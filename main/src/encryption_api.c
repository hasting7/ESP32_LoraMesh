#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "monocypher.h"
#include "esp_random.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_SIZE (256)
#define OK  (1)
#define BAD (0)

// Session keys (ephemeral for this boot)
static uint8_t SK_MY[32], PK_MY[32];

const uint8_t ESCAPE = 0x30;
const uint8_t EMIT_N = 0x31; // \n is 0x0A
const uint8_t EMIT_R = 0x32; // \r is 0x0D
const uint8_t EMIT_E = 0x33;
const uint8_t EMIT_NULL = 0x34;
const uint8_t NEW_L = 0x0A;
const uint8_t CAR_RET = 0x0D;

static void rng(uint8_t *out, size_t n){
	for (int i = 0; i < n; i++) {
		out[i] = (uint8_t) esp_random();
	}
}




// Simple 24-byte nonce: 8-byte random prefix + 16-byte counter (little-endian)
typedef struct { uint8_t prefix[8]; uint64_t ctr_lo, ctr_hi; } nonce24_t;
static void nonce_init(nonce24_t *n){ rng(n->prefix,8); n->ctr_lo = 0; n->ctr_hi = 0; }
static void nonce_next(uint8_t out[24], nonce24_t *n){
	memcpy(out, n->prefix, 8);
	memcpy(out+8,  &n->ctr_lo, 8);
	memcpy(out+16, &n->ctr_hi, 8);
	if (++n->ctr_lo == 0) ++n->ctr_hi;
}


int encrypt_to_peer(const uint8_t PEER_PK[32], const char *pt, uint8_t *out_blob, int max_size)
{
	uint8_t ESK[32], EPK[32], shared[32], aead_key[32], nonce[24], mac[16];

	nonce24_t nc; nonce_init(&nc);

	// ephemeral sender keypair
	rng(ESK, 32);
	crypto_x25519_public_key(EPK, ESK);

	// ECDH and KDF
	crypto_x25519(shared, ESK, PEER_PK);
	crypto_blake2b(aead_key, 32, shared, 32);

	// nonce + encrypt
	nonce_next(nonce, &nc);
	int header_length = 1 + 32 + 24 + 16;
	uint8_t *ct = out_blob + header_length;
	printf("header length = %d\n", header_length);
	uint8_t length = strlen(pt);

	if (length + header_length > max_size) return BAD; 

	crypto_aead_lock(/*cipher_text*/ ct,
		 /*mac*/	 mac,
		 /*key*/	 aead_key,
		 /*nonce*/	 nonce,
		 /*ad*/		 NULL,
		 /*ad_size*/	 0,
		 /*plain_text*/  (const uint8_t *) pt,
		 /*text_size*/	 length);


	length += header_length;
	// pack
	memcpy(out_blob,	&length, 1);
	memcpy(out_blob+1,	    EPK,   32);
	memcpy(out_blob+1+32,     nonce, 24);
	memcpy(out_blob+1+32+24,  mac,   16);

	crypto_wipe(ESK, 32); crypto_wipe(shared, 32); crypto_wipe(aead_key, 32);
	return OK;
}

int decrypt_from_peer(const uint8_t *blob, uint8_t *pt_out)
{
	uint8_t ct_len = *blob;
	
	const uint8_t *EPK	 = blob + 1;
	const uint8_t *nonce = blob + 1+ 32;
	const uint8_t *mac	 = blob + 1 + 32 + 24;
	const uint8_t *ct	 = blob + 1 + 32 + 24 + 16;

	uint8_t shared[32], aead_key[32];
	crypto_x25519(shared, SK_MY, EPK);
	crypto_blake2b(aead_key, 32, shared, 32);

	crypto_aead_unlock(/*plain_text*/	pt_out,
				/*mac*/	   mac,
				/*key*/	   aead_key,
				/*nonce*/	   nonce,
				/*ad*/	   NULL,
				/*ad_size*/    0,
				/*cipher_text*/ct,
				/*text_size*/ ct_len - 1 - 32 - 24 - 16);

	crypto_wipe(shared, 32); crypto_wipe(aead_key, 32);
	return OK;
}


int encode_to_safe_chars(uint8_t *string, size_t max_length) {
	uint8_t buffer[max_length];
	int len = 0;

	uint8_t data_length = *string; // get length from first byte
	printf("data length: %d\n", data_length);
	
	for (int i = 0; i < data_length ; i++) {
		if (string[i] == NEW_L) {
			buffer[len++] = ESCAPE;
			buffer[len++] = EMIT_N;
		} else if (string[i] == CAR_RET) {
			buffer[len++] = ESCAPE;
			buffer[len++] = EMIT_R;
		} else if (string[i] == ESCAPE) {
			buffer[len++] = ESCAPE;
			buffer[len++] = EMIT_E;
		} else if (string[i] == 0x00) {
			buffer[len++] = ESCAPE;
			buffer[len++] = EMIT_NULL;
		} else {
			buffer[len++] = string[i];
		}
	}
	if (len >= max_length) return BAD;
	buffer[len++] = '\0';

	memcpy(string, buffer, max_length);

	return len;
}


int encrypt_message(uint8_t PEER_PK[32], const char *message, uint8_t *out_msg, int max_size) {
	uint8_t buffer[max_size];
	int inital_success = encrypt_to_peer(PEER_PK, message, buffer, max_size);
	if (!inital_success) return -1;
	
	int length_safe_chars = encode_to_safe_chars(buffer, max_size);

	if (!length_safe_chars) return -2;

	memcpy(out_msg, buffer, length_safe_chars);
	return OK;

}

int decrypt_message(const uint8_t *message, char *out_msg, int max_size) {
	uint8_t buffer[max_size];

	int len = 0;

	for (int i = 0; message[i] != 0x0; i++) {
		if (message[i] == ESCAPE) {
			if (message[i + 1] == EMIT_N) {
				buffer[len++] = NEW_L;
			} else if (message[i + 1] == EMIT_R) {
				buffer[len++] = CAR_RET;
			} else if (message[i + 1] == EMIT_NULL) {
				buffer[len++] = 0x0;
			} else {
				buffer[len++] = ESCAPE;
			}
			i++;
		} else {
			buffer[len++] = message[i];
		}
	}
	buffer[len] = '\0';

	return decrypt_from_peer(buffer, (uint8_t *) out_msg);
}

int main(){
	rng(SK_MY, 32);
	crypto_x25519_public_key(PK_MY, SK_MY);

	const char *msg = "hi my name is ben and this is a test of somthing, i think this makes it too long tho thersasf asdfasd fasd asd ada adasdads adaasdasdadasdasdasddasds asdasdasdasdasdasd";

	uint8_t buffer[MAX_SIZE];

	int status = encrypt_message(PK_MY, msg, buffer, MAX_SIZE);
	printf("status = %d\n",status);
	printf("msg = %s\n",buffer);

	char output[MAX_SIZE];
	status = decrypt_message(buffer, output, MAX_SIZE);
	printf("output: %s\n", output);


	return 0;
}

