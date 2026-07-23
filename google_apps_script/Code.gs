// AFMS Google Apps Script v3.1.1
// Bound to spreadsheet: AFMS Monitering
// Configure Script Property AFMS_API_TOKEN before deployment.

const AFMS_SPREADSHEET_ID = '1eAbQ1RlFRNcyOolsraL6AzNmsDtcI_NijozKbs6Xa6c';
const AFMS_TOKEN_PROPERTY = 'AFMS_API_TOKEN';
const REPORT_INDEX_SHEET = 'Report_Index';

const SHEET_HEADERS = {
  Events: [
    'Server Received At', 'Device Timestamp', 'Record Type', 'Event Name',
    'Event Value', 'Loss Code', 'Loss Duration Seconds', 'Machine ID',
    'Machine Name', 'State', 'Shift', 'Operator ID', 'Part Number',
    'Part Name', 'Total', 'Reject', 'Good', 'Target', 'Availability %',
    'Performance %', 'Quality %', 'OEE %', 'Alarm Active', 'Report ID',
    'Loss Name'
  ],
  Hourly_Summary: [
    'Server Received At', 'Device Timestamp', 'Record Type', 'Period End Epoch',
    'Machine ID', 'Machine Name', 'State', 'Shift', 'Operator ID',
    'Part Number', 'Part Name', 'Total', 'Reject', 'Good',
    'Shift Production', 'Shift Reject', 'Shift Good', 'Target',
    'Idle Seconds', 'Run Seconds', 'Downtime Seconds', 'Availability %',
    'Performance %', 'Quality %', 'OEE %', 'Alarm Active', 'Report ID'
  ],
  Shift_Summary: [
    'Server Received At', 'Device Timestamp', 'Record Type', 'Period End Epoch',
    'Machine ID', 'Machine Name', 'Shift', 'Shift Name', 'Operator ID',
    'Part Number', 'Part Name', 'Target', 'Production', 'Reject', 'Good',
    'Availability %', 'Performance %', 'Quality %', 'OEE %', 'Report ID'
  ],
  OEE: [
    'Server Received At', 'Device Timestamp', 'Record Type', 'Period End Epoch',
    'Machine ID', 'Machine Name', 'State', 'Shift', 'Operator ID',
    'Part Number', 'Part Name', 'Total', 'Reject', 'Good', 'Target',
    'Idle Seconds', 'Run Seconds', 'Downtime Seconds', 'Availability %',
    'Performance %', 'Quality %', 'OEE %', 'Alarm Active', 'Report ID'
  ]
};

function doGet() {
  return jsonResponse_({ ok: true, service: 'AFMS', version: '3.1.1' });
}

function doPost(e) {
  const lock = LockService.getScriptLock();
  try {
    lock.waitLock(10000);
    const payload = parsePayload_(e);
    validateToken_(payload);

    const reportId = stringValue_(payload.report_id);
    if (!reportId) {
      return jsonResponse_({ ok: false, error: 'missing_report_id' });
    }

    const indexSheet = ensureIndexSheet_();
    const duplicate = findReportId_(indexSheet, reportId);
    if (duplicate) {
      return jsonResponse_({
        ok: true,
        duplicate: true,
        report_id: reportId,
        sheet: duplicate.sheet,
        row: duplicate.row
      });
    }

    const targetName = targetSheetName_(payload);
    const targetSheet = ensureSheet_(targetName, SHEET_HEADERS[targetName]);
    const row = buildRow_(targetName, payload);
    targetSheet.appendRow(row);
    const appendedRow = targetSheet.getLastRow();

    indexSheet.appendRow([reportId, targetName, appendedRow, new Date()]);
    SpreadsheetApp.flush();

    return jsonResponse_({
      ok: true,
      duplicate: false,
      report_id: reportId,
      sheet: targetName,
      row: appendedRow
    });
  } catch (error) {
    return jsonResponse_({ ok: false, error: String(error && error.message ? error.message : error) });
  } finally {
    try { lock.releaseLock(); } catch (ignored) {}
  }
}

function parsePayload_(e) {
  if (!e || !e.postData || !e.postData.contents) {
    throw new Error('empty_request');
  }
  try {
    return JSON.parse(e.postData.contents);
  } catch (error) {
    throw new Error('invalid_json');
  }
}

function validateToken_(payload) {
  const expected = PropertiesService.getScriptProperties().getProperty(AFMS_TOKEN_PROPERTY);
  if (!expected) throw new Error('server_token_not_configured');
  if (stringValue_(payload.api_token) !== expected) throw new Error('unauthorized');
}

function targetSheetName_(payload) {
  const type = stringValue_(payload.record_type).toLowerCase();
  if (type === 'event' || type === 'loss' || type === 'loss_selected' || type === 'machine_ready') {
    return 'Events';
  }
  if (type === 'hourly_summary' || type === 'hourly' || type === 'recovered_hourly') {
    return 'Hourly_Summary';
  }
  if (type === 'shift_summary' || type === 'shift' || type === 'recovered_shift') {
    return 'Shift_Summary';
  }
  if (type === 'statistics' || type === 'oee' || type === 'monthly_statistics') {
    return 'OEE';
  }
  throw new Error('unsupported_record_type:' + type);
}

function ensureSheet_(name, requiredHeaders) {
  const spreadsheet = SpreadsheetApp.openById(AFMS_SPREADSHEET_ID);
  let sheet = spreadsheet.getSheetByName(name);
  if (!sheet) sheet = spreadsheet.insertSheet(name);
  ensureHeaders_(sheet, requiredHeaders);
  return sheet;
}

function ensureHeaders_(sheet, requiredHeaders) {
  const lastColumn = Math.max(sheet.getLastColumn(), 1);
  const current = sheet.getRange(1, 1, 1, lastColumn).getValues()[0];
  const normalized = current.map(value => stringValue_(value));

  requiredHeaders.forEach(header => {
    if (normalized.indexOf(header) === -1) {
      normalized.push(header);
    }
  });

  if (normalized.length === 0 || normalized.every(value => !value)) {
    sheet.getRange(1, 1, 1, requiredHeaders.length).setValues([requiredHeaders]);
  } else {
    sheet.getRange(1, 1, 1, normalized.length).setValues([normalized]);
  }
  sheet.setFrozenRows(1);
}

function ensureIndexSheet_() {
  const spreadsheet = SpreadsheetApp.openById(AFMS_SPREADSHEET_ID);
  let sheet = spreadsheet.getSheetByName(REPORT_INDEX_SHEET);
  if (!sheet) {
    sheet = spreadsheet.insertSheet(REPORT_INDEX_SHEET);
    sheet.appendRow(['Report ID', 'Sheet', 'Row', 'Received At']);
    sheet.setFrozenRows(1);
    sheet.hideSheet();
  }
  return sheet;
}

function findReportId_(indexSheet, reportId) {
  if (indexSheet.getLastRow() < 2) return null;
  const match = indexSheet
    .getRange(2, 1, indexSheet.getLastRow() - 1, 1)
    .createTextFinder(reportId)
    .matchEntireCell(true)
    .findNext();
  if (!match) return null;
  const row = match.getRow();
  return {
    sheet: stringValue_(indexSheet.getRange(row, 2).getValue()),
    row: Number(indexSheet.getRange(row, 3).getValue()) || 0
  };
}

function buildRow_(sheetName, payload) {
  const sheet = SpreadsheetApp.openById(AFMS_SPREADSHEET_ID).getSheetByName(sheetName);
  const headers = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getValues()[0];
  const values = fieldMap_(payload);
  return headers.map(header => Object.prototype.hasOwnProperty.call(values, header) ? values[header] : '');
}

function fieldMap_(payload) {
  const receivedAt = new Date();
  const deviceTimestamp = parseDeviceDate_(payload.timestamp, payload.created_epoch);
  return {
    'Server Received At': receivedAt,
    'Device Timestamp': deviceTimestamp,
    'Record Type': value_(payload.record_type),
    'Event Name': value_(payload.event_name),
    'Event Value': value_(payload.event_value),
    'Loss Code': value_(payload.loss_code),
    'Loss Name': value_(payload.loss_name),
    'Loss Duration Seconds': firstValue_(payload.loss_duration_seconds, payload.duration_seconds),
    'Machine ID': value_(payload.machine_id),
    'Machine Name': value_(payload.machine_name),
    'State': value_(payload.state),
    'Shift': value_(payload.shift),
    'Shift Name': value_(payload.shift_name),
    'Operator ID': value_(payload.operator_id),
    'Part Number': value_(payload.part_number),
    'Part Name': value_(payload.part_name),
    'Total': value_(payload.total),
    'Reject': value_(payload.reject),
    'Good': value_(payload.good),
    'Shift Production': value_(payload.shift_production),
    'Shift Reject': value_(payload.shift_reject),
    'Shift Good': value_(payload.shift_good),
    'Target': value_(payload.target),
    'Production': firstValue_(payload.production, payload.shift_production),
    'Idle Seconds': value_(payload.idle_seconds),
    'Run Seconds': value_(payload.run_seconds),
    'Downtime Seconds': value_(payload.downtime_seconds),
    'Availability %': permilleToPercent_(payload.availability_permille),
    'Performance %': permilleToPercent_(payload.performance_permille),
    'Quality %': permilleToPercent_(payload.quality_permille),
    'OEE %': permilleToPercent_(payload.oee_permille),
    'Alarm Active': booleanValue_(payload.alarm),
    'Period End Epoch': firstValue_(payload.period_end_epoch, payload.created_epoch),
    'Report ID': value_(payload.report_id)
  };
}

function parseDeviceDate_(timestamp, epoch) {
  if (timestamp) {
    const parsed = new Date(timestamp);
    if (!isNaN(parsed.getTime())) return parsed;
  }
  const seconds = Number(epoch) || 0;
  return seconds > 0 ? new Date(seconds * 1000) : '';
}

function permilleToPercent_(value) {
  const number = Number(value);
  return isNaN(number) ? '' : number / 10;
}

function booleanValue_(value) {
  if (value === true || value === 1 || value === '1') return true;
  if (typeof value === 'string' && value.toLowerCase() === 'true') return true;
  return false;
}

function firstValue_(first, second) {
  return first !== undefined && first !== null && first !== '' ? first : value_(second);
}

function value_(value) {
  return value === undefined || value === null ? '' : value;
}

function stringValue_(value) {
  return value === undefined || value === null ? '' : String(value).trim();
}

function jsonResponse_(payload) {
  return ContentService
    .createTextOutput(JSON.stringify(payload))
    .setMimeType(ContentService.MimeType.JSON);
}

// Run once from the Apps Script editor after replacing YOUR_TOKEN.
function configureAfmsToken() {
  PropertiesService.getScriptProperties().setProperty(AFMS_TOKEN_PROPERTY, 'YOUR_TOKEN');
}
