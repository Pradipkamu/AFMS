/** AFMS v3.0 Google Apps Script receiver.
 * Script Properties required:
 *   AFMS_SPREADSHEET_ID
 *   AFMS_API_TOKEN
 */

const CONFIG = Object.freeze({
  EVENTS_SHEET: 'Events',
  HOURLY_SHEET: 'Hourly_Summary',
  TIME_ZONE: 'Asia/Kolkata',
  LOCK_TIMEOUT_MS: 10000
});

const EVENT_HEADERS = Object.freeze([
  'Server Received At', 'Device Timestamp', 'Record Type', 'Event Name',
  'Event Value', 'Loss Code', 'Loss Duration Seconds', 'Machine ID',
  'Machine Name', 'State', 'Shift', 'Operator ID', 'Part Number', 'Part Name',
  'Total', 'Reject', 'Good', 'Target', 'Availability %', 'Performance %',
  'Quality %', 'OEE %', 'Alarm Active'
]);

const HOURLY_HEADERS = Object.freeze([
  'Server Received At', 'Device Timestamp', 'Machine ID', 'Machine Name',
  'State', 'Shift', 'Operator ID', 'Part Number', 'Part Name', 'Total',
  'Reject', 'Good', 'Shift Production', 'Shift Reject', 'Shift Good',
  'Target', 'Target Remaining', 'Idle Seconds', 'Run Seconds',
  'Downtime Seconds', 'Availability %', 'Performance %', 'Quality %',
  'OEE %', 'Alarm Active'
]);

function doPost(e) {
  let lock;
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return json_({ok: false, error: 'EMPTY_REQUEST_BODY'});
    }

    let data;
    try {
      data = JSON.parse(e.postData.contents);
    } catch (error) {
      return json_({ok: false, error: 'INVALID_JSON', message: error.message});
    }

    const properties = PropertiesService.getScriptProperties();
    const spreadsheetId = properties.getProperty('AFMS_SPREADSHEET_ID');
    const apiToken = properties.getProperty('AFMS_API_TOKEN');

    if (!spreadsheetId || !apiToken) {
      return json_({ok: false, error: 'SCRIPT_PROPERTIES_NOT_CONFIGURED'});
    }

    if (!secureEquals_(String(data.api_token || ''), String(apiToken))) {
      return json_({ok: false, error: 'UNAUTHORIZED'});
    }

    lock = LockService.getScriptLock();
    lock.waitLock(CONFIG.LOCK_TIMEOUT_MS);

    const spreadsheet = SpreadsheetApp.openById(spreadsheetId);
    const recordType = String(data.record_type || '').toLowerCase();
    let result;

    if (recordType === 'hourly_summary') {
      result = appendHourly_(spreadsheet, data);
    } else if (recordType === 'event' || recordType === 'shift_summary') {
      result = appendEvent_(spreadsheet, data);
    } else {
      return json_({ok: false, error: 'UNSUPPORTED_RECORD_TYPE', record_type: recordType});
    }

    SpreadsheetApp.flush();
    return json_({ok: true, sheet: result.sheet, row: result.row, record_type: recordType});
  } catch (error) {
    console.error(error);
    return json_({ok: false, error: 'SERVER_ERROR', message: error.message});
  } finally {
    if (lock && lock.hasLock()) lock.releaseLock();
  }
}

function doGet() {
  return json_({
    ok: true,
    application: 'AFMS Google Sheets Receiver',
    version: '3.0.0',
    timestamp: Utilities.formatDate(new Date(), CONFIG.TIME_ZONE, 'yyyy-MM-dd HH:mm:ss')
  });
}

function initializeAfmsSheets() {
  const properties = PropertiesService.getScriptProperties();
  const spreadsheetId = properties.getProperty('AFMS_SPREADSHEET_ID');
  const apiToken = properties.getProperty('AFMS_API_TOKEN');
  if (!spreadsheetId || !apiToken) throw new Error('Set AFMS_SPREADSHEET_ID and AFMS_API_TOKEN first.');

  const spreadsheet = SpreadsheetApp.openById(spreadsheetId);
  const events = getOrCreate_(spreadsheet, CONFIG.EVENTS_SHEET, EVENT_HEADERS);
  const hourly = getOrCreate_(spreadsheet, CONFIG.HOURLY_SHEET, HOURLY_HEADERS);
  ensureHeaders_(events, EVENT_HEADERS);
  ensureHeaders_(hourly, HOURLY_HEADERS);
  events.getRange('A:B').setNumberFormat('dd-MMM-yyyy HH:mm:ss');
  hourly.getRange('A:B').setNumberFormat('dd-MMM-yyyy HH:mm:ss');
  events.getRange('S:V').setNumberFormat('0.0');
  hourly.getRange('U:X').setNumberFormat('0.0');
  events.autoResizeColumns(1, EVENT_HEADERS.length);
  hourly.autoResizeColumns(1, HOURLY_HEADERS.length);
}

function appendEvent_(spreadsheet, data) {
  const sheet = getOrCreate_(spreadsheet, CONFIG.EVENTS_SHEET, EVENT_HEADERS);
  ensureHeaders_(sheet, EVENT_HEADERS);
  const production = number_(data.production !== undefined ? data.production : data.total);
  const reject = number_(data.reject);
  const good = data.good !== undefined ? number_(data.good) : Math.max(0, production - reject);
  const isLoss = String(data.event_name || '') === 'loss_selected';

  const row = [
    new Date(), timestamp_(data.timestamp), text_(data.record_type),
    text_(data.event_name || (data.record_type === 'shift_summary' ? 'shift_summary' : '')),
    number_(data.event_value), isLoss ? number_(data.loss_code || data.event_value) : '',
    isLoss ? number_(data.loss_duration_seconds || data.duration_seconds) : '',
    text_(data.machine_id), text_(data.machine_name), number_(data.state),
    number_(data.shift), number_(data.operator_id), number_(data.part_number),
    text_(data.part_name), production, reject, good, number_(data.target),
    percent_(data.availability_permille), percent_(data.performance_permille),
    percent_(data.quality_permille), percent_(data.oee_permille), Boolean(data.alarm)
  ];

  sheet.appendRow(row);
  const rowNumber = sheet.getLastRow();
  sheet.getRange(rowNumber, 1, 1, 2).setNumberFormat('dd-MMM-yyyy HH:mm:ss');
  sheet.getRange(rowNumber, 19, 1, 4).setNumberFormat('0.0');
  return {sheet: sheet.getName(), row: rowNumber};
}

function appendHourly_(spreadsheet, data) {
  const sheet = getOrCreate_(spreadsheet, CONFIG.HOURLY_SHEET, HOURLY_HEADERS);
  ensureHeaders_(sheet, HOURLY_HEADERS);
  const target = number_(data.target);
  const shiftGood = number_(data.shift_good);

  const row = [
    new Date(), timestamp_(data.timestamp), text_(data.machine_id),
    text_(data.machine_name), number_(data.state), number_(data.shift),
    number_(data.operator_id), number_(data.part_number), text_(data.part_name),
    number_(data.total), number_(data.reject), number_(data.good),
    number_(data.shift_production), number_(data.shift_reject), shiftGood,
    target, Math.max(0, target - shiftGood), number_(data.idle_seconds),
    number_(data.run_seconds), number_(data.downtime_seconds),
    percent_(data.availability_permille), percent_(data.performance_permille),
    percent_(data.quality_permille), percent_(data.oee_permille), Boolean(data.alarm)
  ];

  sheet.appendRow(row);
  const rowNumber = sheet.getLastRow();
  sheet.getRange(rowNumber, 1, 1, 2).setNumberFormat('dd-MMM-yyyy HH:mm:ss');
  sheet.getRange(rowNumber, 21, 1, 4).setNumberFormat('0.0');
  return {sheet: sheet.getName(), row: rowNumber};
}

function getOrCreate_(spreadsheet, name, headers) {
  let sheet = spreadsheet.getSheetByName(name);
  if (!sheet) sheet = spreadsheet.insertSheet(name);
  if (sheet.getLastRow() === 0) {
    sheet.getRange(1, 1, 1, headers.length).setValues([headers]);
    sheet.getRange(1, 1, 1, headers.length).setFontWeight('bold').setWrap(true);
    sheet.setFrozenRows(1);
  }
  return sheet;
}

function ensureHeaders_(sheet, headers) {
  sheet.getRange(1, 1, 1, headers.length).setValues([headers]);
  sheet.getRange(1, 1, 1, headers.length).setFontWeight('bold').setWrap(true);
  sheet.setFrozenRows(1);
}

function json_(value) {
  return ContentService.createTextOutput(JSON.stringify(value))
    .setMimeType(ContentService.MimeType.JSON);
}

function timestamp_(value) {
  if (!value) return '';
  const date = new Date(value);
  return isNaN(date.getTime()) ? text_(value) : date;
}

function percent_(permille) {
  const value = Number(permille);
  return isFinite(value) ? Math.max(0, Math.min(100, value / 10)) : 0;
}

function number_(value) {
  const number = Number(value);
  return isFinite(number) && number >= 0 ? Math.floor(number) : 0;
}

function text_(value) {
  return value === undefined || value === null ? '' : String(value);
}

function secureEquals_(left, right) {
  let difference = left.length ^ right.length;
  const maximum = Math.max(left.length, right.length);
  for (let index = 0; index < maximum; index++) {
    difference |= (left.charCodeAt(index) || 0) ^ (right.charCodeAt(index) || 0);
  }
  return difference === 0;
}
