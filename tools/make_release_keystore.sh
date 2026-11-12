#!/usr/bin/env bash
# make_release_keystore.sh - one-time: make the signing key the release APKs are signed with, named "Destroyer And Nitro".
#
# With a stable key every build installs over the previous one (a build-specific key does not).  The key is YOURS: it is never part of
# the repository.  Keep the .jks and the password somewhere safe (a lost key means a new app identity: uninstall + reinstall); anyone who
# has them can sign an app as you.  Needs the JDK's keytool and openssl.
#
#   tools/make_release_keystore.sh [output.jks]
#
# It prints the three values to store as GitHub repository secrets (Settings -> Secrets and variables -> Actions):
#   ZG_KEYSTORE_B64, ZG_KEYSTORE_PASSWORD, ZG_KEY_ALIAS      (the android-apk workflow picks them up on the next push)
set -euo pipefail
OUT="${1:-zgirls-release.jks}"
ALIAS="zgirls"
[ -e "$OUT" ] && { echo "$OUT already exists: not overwriting it" >&2; exit 1; }
PASS=$(openssl rand -hex 16)
keytool -genkeypair -keystore "$OUT" -storetype PKCS12 -alias "$ALIAS" -keyalg RSA -keysize 3072 -validity 9125 \
  -storepass "$PASS" -keypass "$PASS" -dname "CN=Destroyer And Nitro, O=Destroyer And Nitro"
echo
echo "Keystore written to $OUT  (keep it private, do not commit it)"
echo "Certificate:"; keytool -list -v -keystore "$OUT" -storepass "$PASS" | grep -E "Owner:|SHA256:|Valid from"
echo
echo "Repository secrets to add:"
echo "  ZG_KEYSTORE_PASSWORD = $PASS"
echo "  ZG_KEY_ALIAS         = $ALIAS"
echo "  ZG_KEYSTORE_B64      = (the base64 below, one line)"
base64 < "$OUT" | tr -d '\n'; echo
