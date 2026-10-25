#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    printf 'Usage: %s <raspberry-pi-hostname>\n' "$0" >&2
    exit 2
fi

hostname=$1
case "$hostname" in
    *[!A-Za-z0-9.-]* | .* | *.)
        printf 'Invalid DNS hostname: %s\n' "$hostname" >&2
        exit 2
        ;;
esac

short_hostname=${hostname%%.*}
if [ "$hostname" = "$short_hostname" ]; then
    local_hostname="${hostname}.local"
else
    local_hostname="${short_hostname}.local"
fi

certificate_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/certs
mkdir -p "$certificate_dir"

if [ -e "$certificate_dir/ca.key" ] ||
    [ -e "$certificate_dir/ca.crt" ] ||
    [ -e "$certificate_dir/server.key" ] ||
    [ -e "$certificate_dir/server.crt" ]; then
    printf 'Certificate files already exist in %s; refusing to overwrite them.\n' \
        "$certificate_dir" >&2
    exit 1
fi

temporary_config=$(mktemp)
trap 'rm -f "$temporary_config"' EXIT HUP INT TERM

umask 077
openssl genpkey -algorithm EC \
    -pkeyopt ec_paramgen_curve:P-256 \
    -out "$certificate_dir/ca.key"
openssl req -x509 -new -sha256 -days 3650 \
    -key "$certificate_dir/ca.key" \
    -out "$certificate_dir/ca.crt" \
    -subj "/CN=HomeNodes Local CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -addext "subjectKeyIdentifier=hash"

openssl genpkey -algorithm EC \
    -pkeyopt ec_paramgen_curve:P-256 \
    -out "$certificate_dir/server.key"
openssl req -new \
    -key "$certificate_dir/server.key" \
    -out "$certificate_dir/server.csr" \
    -subj "/CN=$hostname"

cat >"$temporary_config" <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:$hostname,DNS:$local_hostname
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid,issuer
EOF

openssl x509 -req -sha256 -days 825 \
    -in "$certificate_dir/server.csr" \
    -CA "$certificate_dir/ca.crt" \
    -CAkey "$certificate_dir/ca.key" \
    -CAcreateserial \
    -out "$certificate_dir/server.crt" \
    -extfile "$temporary_config"
rm -f "$certificate_dir/server.csr" "$certificate_dir/ca.srl"
chmod 600 "$certificate_dir/ca.key" "$certificate_dir/server.key"
chmod 644 "$certificate_dir/ca.crt" "$certificate_dir/server.crt"

printf '\nCreated a local CA and HTTPS certificate for:\n  %s\n  %s\n' \
    "$hostname" "$local_hostname"
printf 'Install this public CA certificate in the browser device trust store:\n  %s/ca.crt\n' \
    "$certificate_dir"
printf 'Keep ca.key private. Never copy it to browser devices or commit certs/.\n'
