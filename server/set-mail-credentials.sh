#!/bin/sh
# Write /opt/reasixty/.env with the SMTP credentials, then restart and check.
#
#   ssh root@srv1401714.hstgr.cloud
#   sh /opt/reasixty/set-mail-credentials.sh
#
# WHY A SCRIPT AND NOT A BLOCK TO PASTE. A runbook that starts with `ssh` and
# continues with commands meant for the far end does not survive being pasted
# in one go: ssh connects and swallows the rest. That happened. One command
# that prompts for one secret cannot be pasted wrongly.
#
# The password is read with `read -rs`, so it never reaches the shell history
# nor the process list — typing it into a command line would put it in
# ~/.bash_history and make it visible to `ps` for every user on the box.

set -eu

APP_DIR=${APP_DIR:-/opt/reasixty}
ENV_FILE="$APP_DIR/.env"

SMTP_HOST=${SMTP_HOST:-asmtp.mail.hostpoint.ch}
SMTP_PORT=${SMTP_PORT:-587}
SMTP_USER=${SMTP_USER:-info@stoersender-studio.ch}
MAIL_FROM=${MAIL_FROM:-Rea-Sixty <info@stoersender-studio.ch>}
SITE_ORIGIN=${SITE_ORIGIN:-https://reasixty.com}
# The name announced in EHLO. Must be a hostname whose forward and reverse DNS
# both agree with the IP the mail leaves from, or receivers treat it as a spam
# signal. Without it nodemailer announces the container's hostname, which came
# out as the literal [127.0.0.1] in a real delivery.
SMTP_HELO=${SMTP_HELO:-$(hostname -f 2>/dev/null || hostname)}

cd "$APP_DIR"

if [ -f "$ENV_FILE" ]; then
    printf '%s already exists. Overwrite it? [y/N] ' "$ENV_FILE"
    read -r reply
    case "$reply" in y|Y) ;; *) echo 'left alone.'; exit 0 ;; esac
fi

printf 'SMTP password for %s: ' "$SMTP_USER"
stty -echo 2>/dev/null || true
read -r PASS
stty echo 2>/dev/null || true
echo

if [ -z "$PASS" ]; then
    echo 'nothing entered — no file written.' >&2
    exit 1
fi

# 600 from the moment it exists. chmod afterwards leaves a window in which the
# file is world-readable, and a secret only has to leak once.
umask 077
printf '%s\n' \
    "SMTP_HOST=$SMTP_HOST" \
    "SMTP_PORT=$SMTP_PORT" \
    "SMTP_USER=$SMTP_USER" \
    "SMTP_PASS=$PASS" \
    "MAIL_FROM=$MAIL_FROM" \
    "SMTP_HELO=$SMTP_HELO" \
    "SITE_ORIGIN=$SITE_ORIGIN" > "$ENV_FILE"
PASS=
chown root:root "$ENV_FILE"

echo
echo "wrote $ENV_FILE:"
# Keys only. Printing the file to confirm it "looks right" is how a password
# ends up in a terminal scrollback and, from there, in a screenshot.
sed 's/=.*/=…/' "$ENV_FILE" | sed 's/^/  /'
ls -l "$ENV_FILE"

echo
echo 'restarting the exchange…'
docker compose up -d

echo
echo 'waiting for the mail transport…'
sleep 4
if docker compose logs --tail 50 2>&1 | grep -qi 'SMTP transport ready'; then
    echo 'OK — mail: SMTP transport ready'
else
    echo 'NOT confirmed. The container log says:' >&2
    docker compose logs --tail 15 2>&1 | sed 's/^/  /' >&2
    echo >&2
    echo 'A wrong password does not show up here — SMTP only authenticates on' >&2
    echo 'the first send. Test it for real by requesting a sign-in link.' >&2
    exit 1
fi
