const AFMS_CONFIG = Object.freeze({
  HOURLY_SHEET: 'Hourly Summary',
  EVENT_SHEET: 'Events',
  TIME_ZONE: 'Asia/Kolkata',
  TOKEN_PROPERTY: 'AFMS_API_TOKEN'
});

const HOURLY_HEADERS = Object.freeze([
  'Received At',
  'Timestamp',
  'Period End',
  'Machine ID',
  'Machine Name',
  'State',
  'Shift',
  'Operator ID',
  'Part Number',
  'Part Name',
  'Total',
  'Reject',
  'Good',
  'Shift Production',
  'Shift Reject',
  'Shift Good',
  'Target',
  'Idle Seconds',
  'Run Seconds',
  'Downtime Seconds',
  'Availability %',
  'Performance %',
  'Quality %',
  'OEE %',
  'Alarm',
  'Upload Delay Seconds'
]);

const EVENT_HEADERS = Object.freeze([
  'Received At',
  'Timestamp',
  'Machine ID',
  'Machine Name',
  'Event Name',
  'Event Value',
  'Duration Seconds',
  'Loss Code',
  'Loss Name',
  'Loss Duration Seconds',
  'State',
  'Shift',
  'Operator ID',
  'Part Number',
  'Part Name',
  'Total',
  'Reject',
  'Good',
  'Alarm'
]);

function doGet() {
  return jsonResponse_({
    ok: true,
    service: 'AFMS Google Sheets receiver',
    version: '3.1.0',
    timestamp: new Date().toISOString()
  });
}

function doPost(e) {
  const lock = LockService.getScriptLock();

  try {
    lock.waitLock(10000);

    const payload = parsePayload_(e);
    validateToken_(payload.api_token);

    const recordType = String(payload.record_type || '').trim().toLowerCase();
    if (recordType === 'hourly_summary') {
      appendHourlySummary_(payload);
    } else if (recordType === 'event') {
      appendEvent_(payload);
    } else {
      throw new Error('Unsupported record_type: ' + recordType);
    }

    return jsonResponse_({
      ok: true,
      record_type: recordType,
      received_at: new Date().toISOString()
    });
  } catch (error) {
    console.error(error && error.stack ? error.stack : error);
    return jsonResponse_({
      ok: false,
      error: error && error.message ? error.message : String(error)
    });
  } finally {
    try {
      lock.releaseLock();
    } catch (ignored) {
      // Lock was not acquired or was already released.
    }
  }
}

function appendHourlySummary_(data) {
  const sheet = getOrCreateSheet_(AFMS_CONFIG.HOURLY_SHEET, HOURLY_HEADERS);
  const periodEndEpoch = number_(data.period_end_epoch, 0);
  const periodEnd = periodEndEpoch > 0 ? new Date(periodEndEpoch * 1000) : '';

  sheet.appendRow([
    new Date(),
    text_(data.timestamp),
    periodEnd,
    text_(data.machine_id),
    text_(data.machine_name),
    number_(data.state, 0),
    number_(data.shift, 0),
    number_(data.operator_id, 0),
    number_(data.part_number, 0),
    text_(data.part_name),
    number_(data.total, 0),
    number_(data.reject, 0),
    number_(data.good, 0),
    number_(data.shift_production, 0),
    number_(data.shift_reject, 0),
    number_(data.shift_good, 0),
    number_(data.target, 0),
    number_(data.idle_seconds, 0),
    number_(data.run_seconds, 0),
    number_(data.downtime_seconds, 0),
    permilleToPercent_(data.availability_permille),
    permilleToPercent_(data.performance_permille),
    permilleToPercent_(data.quality_permille),
    permilleToPercent_(data.oee_permille),
    boolean_(data.alarm),
    number_(data.upload_delay_seconds, 0)
  ]);
}

function appendEvent_(data) {
  const sheet = getOrCreateSheet_(AFMS_CONFIG.EVENT_SHEET, EVENT_HEADERS);
  const eventName = text_(data.event_name);
  const isLoss = eventName === 'loss_selected';

  // New firmware fields are preferred. Older firmware remains supported by
  // falling back to event_value and duration_seconds.
  const lossCode = isLoss
    ? number_(data.loss_code !== undefined ? data.loss_code : data.event_value, 0)
    : '';
  const lossName = isLoss ? text_(data.loss_name) : '';
  const lossDuration = isLoss
    ? number_(
        data.loss_duration_seconds !== undefined
          ? data.loss_duration_seconds
          : data.duration_seconds,
        0
      )
    : '';

  sheet.appendRow([
    new Date(),
    text_(data.timestamp),
    text_(data.machine_id),
    text_(data.machine_name),
    eventName,
    number_(data.event_value, 0),
    number_(data.duration_seconds, 0),
    lossCode,
    lossName,
    lossDuration,
    number_(data.state, 0),
    number_(data.shift, 0),
    number_(data.operator_id, 0),
    number_(data.part_number, 0),
    text_(data.part_name),
    number_(data.total, 0),
    number_(data.reject, 0),
    number_(data.good, 0),
    boolean_(data.alarm)
  ]);
}

function parsePayload_(e) {
  if (!e || !e.postData || !e.postData.contents) {
    throw new Error('Empty POST body');
  }

  try {
    return JSON.parse(e.postData.contents);
  } catch (error) {
    throw new Error('Invalid JSON body');
  }
}

function validateToken_(receivedToken) {
  const expectedToken = PropertiesService.getScriptProperties()
    .getProperty(AFMS_CONFIG.TOKEN_PROPERTY);

  // Token validation is optional until AFMS_API_TOKEN is configured in Script
  // Properties. Once configured, every upload must match it exactly.
  if (!expectedToken) return;
  if (String(receivedToken || '') !== expectedToken) {
    throw new Error('Unauthorized API token');
  }
}

function getOrCreateSheet_(name, headers) {
  const spreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  if (!spreadsheet) {
    throw new Error('Apps Script must be bound to the AFMS spreadsheet');
  }

  let sheet = spreadsheet.getSheetByName(name);
  if (!sheet) sheet = spreadsheet.insertSheet(name);

  ensureHeaders_(sheet, headers);
  return sheet;
}

function ensureHeaders_(sheet, headers) {
  const lastColumn = Math.max(sheet.getLastColumn(), headers.length);
  const current = sheet.getRange(1, 1, 1, lastColumn).getValues()[0];

  let needsUpdate = sheet.getLastRow() === 0;
  for (let index = 0; index < headers.length && !needsUpdate; index += 1) {
    if (String(current[index] || '') !== headers[index]) needsUpdate = true;
  }

  if (!needsUpdate) return;

  // Never insert a new header into the middle of historical data. The complete
  // current schema is written to row 1, while existing data rows remain intact.
  sheet.getRange(1, 1, 1, headers.length).setValues([headers]);
  sheet.setFrozenRows(1);
  sheet.getRange(1, 1, 1, headers.length).setFontWeight('bold');

  const receivedColumn = 1;
  sheet.getRange(2, receivedColumn, Math.max(sheet.getMaxRows() - 1, 1), 1)
    .setNumberFormat('yyyy-mm-dd hh:mm:ss');
}

function text_(value) {
  return value === undefined || value === null ? '' : String(value);
}

function number_(value, fallback) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function boolean_(value) {
  if (value === true || value === 1 || value === '1') return true;
  return String(value || '').toLowerCase() === 'true';
}

function permilleToPercent_(value) {
  return number_(value, 0) / 10;
}

function jsonResponse_(body) {
  return ContentService
    .createTextOutput(JSON.stringify(body))
    .setMimeType(ContentService.MimeType.JSON);
}
