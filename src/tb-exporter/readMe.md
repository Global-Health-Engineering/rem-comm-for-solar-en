
This folder fetches data from thingsboard, generates CSV, sends email, uploads to Dropbox automatically using a cronjob.

It contains the following files:

config.env: contains passkeys and other confidential informations

dropbox_auth.js: One-time (or occasional) script to authenticate with Dropbox and generate a dropbox_tokens.json file containing refresh tokens.
Not automatic — you run it manually when you need to (e.g., first-time setup or if refresh tokens stop working).


exporter.js: The main script — logs in, fetches data, generates CSV, sends email, uploads to Dropbox. This is what the cronjob executes weekly.


package.json: Defines the Node.js project — dependencies, scripts, metadata. Used by npm.

package-lock.json: Exact dependency versions — ensures installs are reproducible.


// ----------------------

To set up a crontab, enter crontab -e in the terminal.
Create a new job, for example:
0 8 * * 1 cd /home/lora-pi/tb-exporter && /usr/bin/node exporter.js >> /home/lora-pi/tb-exporter/cron.log 2>&1
This runs the exporter.js script every Monday at 8:00 a.m.
Save the job with Ctrl+O, Enter, and Ctrl+X.

For the scripts to run on the Raspberry Pi, confidential data must be added to config.env. 
The required libraries also need to be installed via the terminal.
Then, run the dropbox-auth.js script, which generates the tokens for Dropbox.
The functionality can be tested by running the Node.js script directly in the terminal.