//ETH Zürich
//Global Health Engineering Lab
//Masterthesis Voeten Jerun
//August 2025

// dropbox_auth.js
//require('dotenv').config();
require('dotenv').config({ path: 'config.env' });
const express = require('express');
const axios = require('axios');
const fs = require('fs');


const {
  DROPBOX_APP_KEY, DROPBOX_APP_SECRET, DROPBOX_REDIRECT_URI
} = process.env;

if (!DROPBOX_APP_KEY || !DROPBOX_APP_SECRET || !DROPBOX_REDIRECT_URI) {
  console.error('Bitte DROPBOX_APP_KEY, DROPBOX_APP_SECRET und DROPBOX_REDIRECT_URI in .env setzen.');
  process.exit(1);
}

const app = express();

const authUrl = `https://www.dropbox.com/oauth2/authorize?response_type=code&client_id=${encodeURIComponent(DROPBOX_APP_KEY)}&token_access_type=offline&redirect_uri=${encodeURIComponent(DROPBOX_REDIRECT_URI)}`;

console.log(`Open this url and authorize it: ${authUrl}`);


app.get('/auth', async (req, res) => {
  const code = req.query.code;
  if (!code) {
    res.status(400).send('No code returned.');
    return;
  }
  try {
    const params = new URLSearchParams();
    params.append('code', code);
    params.append('grant_type', 'authorization_code');
    params.append('client_id', DROPBOX_APP_KEY);
    params.append('client_secret', DROPBOX_APP_SECRET);
    params.append('redirect_uri', DROPBOX_REDIRECT_URI);

    const tokenResp = await axios.post('https://api.dropboxapi.com/oauth2/token', params.toString(), {
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
    });

    const data = tokenResp.data;
    // save access + refresh + expires_at
    const now = Date.now();
    const tokenFile = {
      access_token: data.access_token,
      refresh_token: data.refresh_token,
      expires_at: now + (data.expires_in || 0) * 1000
    };

    fs.writeFileSync('dropbox_tokens.json', JSON.stringify(tokenFile, null, 2), { mode: 0o600 });
    console.log('Saved tokens to dropbox_tokens.json');
    res.send('The Token has been saved. You can close this window.');

    // wait & end process
    setTimeout(() => process.exit(0), 1000);
  } catch (err) {
    console.error('Token exchange failed:', err.response?.data || err.message);
    res.status(500).send('Token exchange failed: ' + (err.message || ''));
  }
});

// read Port from redirect URI (default 3000)
const port = new URL(process.env.DROPBOX_REDIRECT_URI).port || 3000;
app.listen(port, () => console.log('Listening on port', port));
