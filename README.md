# AAAOS
The Operation System (OS) base on ESP8266_RTOS_SDK


## How to OTA
### Create a OTA server for test ###

* openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365</br>
* openssl rsa -in ca_key.pem -out ca_key.pem</br>
* cp ca_cert.pem ../esp/server_certs/</br>

### Run OTA server ###

* openssl s_server -WWW -key ca_key.pem -cert ca_cert.pem -port 8070</br>

### Test OTA server ###

* curl -v https://<ip>:<port>/<file></br>

### Error code ###
* -0x7f00 : MBEDTLS_ERR_SSL_ALLOC_FAILED</br>
* -0x7200 : MBEDTLS_ERR_SSL_INVALID_RECORD, Buffer too low</br>

### Mosqutto ###
mosquitto_sub -v -d -h mqtt.eclipse.org -t dev/up/#</br>
