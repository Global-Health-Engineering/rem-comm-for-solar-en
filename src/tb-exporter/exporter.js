//ETH Zürich
//Global Health Engineering Lab
//Masterthesis Voeten Jerun
//August 2025

require('dotenv').config({ path: 'config.env' });
const axios = require('axios');
const { stringify } = require('csv-stringify/sync');
const nodemailer = require('nodemailer');
const { Dropbox } = require('dropbox');
const fetch = require('node-fetch');
const fs = require('fs');

const {
  TB_URL, TB_USER, TB_PASS, DEVICE_ID,
  DROPBOX_APP_KEY, DROPBOX_APP_SECRET,
  SMTP_HOST, SMTP_PORT, SMTP_USER, SMTP_PASS, MAIL_TO
} = process.env;

const TOKEN_FILE = './dropbox_tokens.json';

// ------------------ Helper func for file names ------------------
function generateFilename() {
  const now = new Date();
  const formatted = new Intl.DateTimeFormat('sv-SE', {
    timeZone: 'Europe/Zurich', //'America/Bogota'; for colombia
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false
  }).format(now).replace(/\s+/, '_').replace(/:/g, '-'); // => "YYYY-MM-DD_HH-MM-SS"

  return `tezhumke_7days_data_export_${formatted}.csv`;
}


// ------------------ ThingsBoard-Functions ------------------
async function login() {
  const resp = await axios.post(
    `${TB_URL}/api/auth/login`,
    { username: TB_USER, password: TB_PASS },
    { headers: { 'Content-Type': 'application/json' } }
  );
  return resp.data.token;
}

async function fetchTelemetry(jwt, startTs, endTs) {
  const url = `${TB_URL}/api/plugins/telemetry/DEVICE/${DEVICE_ID}/values/timeseries`;
  const params = {
    startTs,
    endTs,
    keys: `node,InNodeBatSOC,InNodeBatStatus,fridgeTemp,fridgeHum,InDHTStatus,mainBatSOC,mainBatVoltage,mainBatCurrent,consumed_mAh,chargeCycles,SmartShuntStatus,eBikeState1,eBikeState2,OutNodeBatSOC,outNodeBatStatus,outTemp,outHum,OutDHTStatus,illuminance,OutLightStatus,windspeed,OutWindStatus,repNodeBatSOC,repNodeBatStatus`,
    limit: 50000
  };
  const resp = await axios.get(url, {
    headers: { Authorization: `Bearer ${jwt}` },
    params
  });
  return resp.data;
}

function toCsv(data) {
  const keys = Object.keys(data);
  const allTimestampsSet = new Set();
  for (const key of keys) {
    data[key].forEach(point => allTimestampsSet.add(point.ts));
  }
  const allTimestamps = Array.from(allTimestampsSet).sort((a, b) => a - b);
  const valueMap = {};
  for (const key of keys) {
    valueMap[key] = new Map(data[key].map(p => [p.ts, p.value]));
  }
  const timeZone = 'Europe/Zurich'; //'America/Bogota'; for colombia
  const rows = allTimestamps.map(ts => {
    const date = new Date(Number(ts));
    const isoLocal = date.toLocaleString('sv-SE', {
      timeZone,
      hour12: false
    }).replace('T', ' ');
    const values = keys.map(k => valueMap[k].get(ts) || '');
    return [isoLocal, ...values];
  });
  return stringify(rows, {
    header: true,
    columns: ['timestamp', ...keys],
  });
}

// ------------------ send Mail  ------------------
async function sendMail(csv, filename) {
  const transporter = nodemailer.createTransport({
    host: SMTP_HOST,
    port: parseInt(SMTP_PORT),
    secure: false,
    auth: { user: SMTP_USER, pass: SMTP_PASS }
  });

  await transporter.sendMail({
    from: SMTP_USER,
    to: MAIL_TO,
    subject: 'ThingsBoard: 7-day-CSV-Export',
    text: '',
    attachments: [{ filename, content: csv }]
  });
}

// ------------------ Token Management ------------------
async function getTokensFromFile() {
  try {
    return JSON.parse(fs.readFileSync(TOKEN_FILE, 'utf8'));
  } catch (e) {
    return null;
  }
}

async function saveTokensToFile(tokens) {
  fs.writeFileSync(TOKEN_FILE, JSON.stringify(tokens, null, 2), { mode: 0o600 });
}

async function getAccessToken() {
  const tokens = await getTokensFromFile();
  if (!tokens || !tokens.refresh_token) {
    throw new Error('no dropbox_tokens.json found or no refresh_token available. run dropbox_auth.js once.');
  }

  const now = Date.now();
  if (!tokens.access_token || !tokens.expires_at || tokens.expires_at - 60000 < now) {
    const params = new URLSearchParams();
    params.append('grant_type', 'refresh_token');
    params.append('refresh_token', tokens.refresh_token);
    params.append('client_id', DROPBOX_APP_KEY);
    params.append('client_secret', DROPBOX_APP_SECRET);

    const resp = await axios.post('https://api.dropboxapi.com/oauth2/token', params.toString(), {
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
    });

    const data = resp.data;
    tokens.access_token = data.access_token;
    tokens.expires_at = Date.now() + (data.expires_in || 0) * 1000;
    await saveTokensToFile(tokens);
    console.log('Refreshed Dropbox access_token, new expiry:', new Date(tokens.expires_at).toISOString());
  }
  return tokens.access_token;
}

// ------------------ Upload on Dropbox ------------------
async function uploadToDropbox(csv, filename) {
  const accessToken = await getAccessToken();
  const dbx = new Dropbox({ accessToken, fetch });

  const dropboxPath = `/${filename}`;
  try {
    const response = await dbx.filesUpload({
      path: dropboxPath,
      contents: csv,
      mode: { '.tag': 'overwrite' }
    });
    console.log(`Uploaded to Dropbox: ${response.result.path_display}`);
  } catch (err) {
    console.error('Dropbox upload failed:', err.response?.data || err.message || err);
    throw err;
  }
}

// ------------------ Main  ------------------
(async () => {
  try {
    const now = Date.now();
    const startTs = now - 7 * 24 * 60 * 60 * 1000;
    const endTs = now;
    const filename = generateFilename();

    console.log('Login…');
    const jwt = await login();

    console.log('Get data…');
    const data = await fetchTelemetry(jwt, startTs, endTs);

    console.log('Generate CSV...');
    const csv = toCsv(data);

    console.log('Send email…');
    await sendMail(csv, filename);

    console.log('Upload to Dropbox…');
    await uploadToDropbox(csv, filename);

    console.log('Done');
  } catch (e) {
    console.error('Error:', e.response?.data || e.message || e);
    process.exit(1);
  }
})();
