#!/bin/sh

cp ../esp/build/*.bin latest/

openssl s_server -WWW -key ca_key.pem -cert ca_cert.pem -port 8070

