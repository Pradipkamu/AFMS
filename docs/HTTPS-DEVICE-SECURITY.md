# AFMS HTTPS and device-secret deployment

## Production machine.json

```json
{
  "afms_server_url": "https://afms.example.com",
  "device_api_key": "STORE-ONLY-ON-THE-DEVICE",
  "afms_tls_fingerprint": "AA BB CC DD ...",
  "communication": {
    "afmsWeb": {
      "requireHttps": true
    }
  }
}
```

When `requireHttps` is true, the firmware rejects an `http://` AFMS endpoint. HTTPS connections require a configured certificate fingerprint and are verified by the ESP8266 TLS client.

## Key handling

- Generate a unique key for every device.
- Never commit real keys to GitHub.
- Store only the server-side hash in PostgreSQL.
- Rotate a key immediately when a controller is replaced or lost.
- Disable the old device record before commissioning a replacement.
- Do not print API keys in serial or server logs.

## Oracle/Nginx requirements

- Point a DNS name to the Oracle VM public IP.
- Obtain a TLS certificate with Certbot/Let's Encrypt.
- Redirect port 80 to 443.
- Proxy `/api/` to the backend only through Nginx.
- Keep PostgreSQL and the Node.js backend ports private.

After certificate renewal, update the ESP fingerprint before the old certificate expires. A future CA-certificate implementation should replace fingerprint pinning to avoid manual rotation.
