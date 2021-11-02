/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "asinine/asn1.h"
#include "asinine/errors.h"
#include "internal/macros.h"

#define SECONDS_PER_YEAR (31536000)
#define SECONDS_PER_MONTH (2629744)
#define SECONDS_PER_DAY (86400)
#define SECONDS_PER_HOUR (3600)
#define SECONDS_PER_MINUTE (60)

/** (Y,) Y, M, D, H, M, S "Z" */
#define MIN_TIME_LENGTH (6 * 2 + 1)
#define MAX_TIME_LENGTH (7 * 2 + 1)

static asinine_err_t
validate_string(const asn1_token_t *token) {
	const uint8_t *data;
	const uint8_t *const data_end = token->data + token->length;

	if (token->type.class != ASN1_CLASS_UNIVERSAL) {
		return ERROR(ASININE_ERR_MALFORMED, "string: not of class universal");
	}

	switch (token->type.tag) {
	case ASN1_TAG_PRINTABLESTRING:
		for (data = token->data; data < data_end; data++) {
			// Space
			if (*data == 0x20) {
				continue;
			}

			// ' and z
			if (*data < 0x27 || *data > 0x7a) {
				return ERROR(
				    ASININE_ERR_MALFORMED, "string: invalid character");
			}

			// Illegal characters: *, ;, <, >, @
			if (*data == 0x2a || *data == 0x3b || *data == 0x3c ||
			    *data == 0x3e || *data == 0x40) {
				return ERROR(
				    ASININE_ERR_MALFORMED, "string: invalid character");
			}
		}
		break;

	case ASN1_TAG_IA5STRING:
	case ASN1_TAG_VISIBLESTRING:
	case ASN1_TAG_T61STRING:
		for (data = token->data; data < data_end; data++) {
			/* Strictly speaking, control codes are allowed for IA5STRING,
			 * but since we don't have a way of dealing with code-page
			 * switching we restrict the type. This is non-conformant to the
			 * spec. Same goes for T61String, which can switch code pages
			 * mid-stream. We assume that the initial code-page is #6
			 * (ASCII), and flag switching as an error.
			 */
			if (*data < 0x20 || *data > 0x7f) {
				return ERROR(
				    ASININE_ERR_MALFORMED, "string: invalid character");
			}
		}
		break;

	case ASN1_TAG_UTF8STRING: {
		enum { LEADING, CONTINUATION } state;
		int bytes;

		state = LEADING;
		bytes = 0;

		for (data = token->data; data < data_end; data++) {
			uint8_t byte = *data;

			switch (state) {
			case LEADING:
				if (byte < 0x80) {
					continue;
				}

				if (0xC2 <= byte && byte < 0xD0) {
					bytes = 1;
				} else if (0xD0 <= byte && byte < 0xF5) {
					bytes = (byte >> 4) - 0xC;
				} else {
					// 0x80 - 0xBF: Continuation bytes
					// 0xC0 - 0xC1: Invalid code points
					return ERROR(ASININE_ERR_MALFORMED,
					    "utf8string: invalid leading character");
				}

				state = CONTINUATION;
				break;

			case CONTINUATION:
				if (byte < 0x80 || byte >= 0xC0) {
					return ERROR(ASININE_ERR_MALFORMED,
					    "utf8string: invalid continuation");
				}

				bytes -= 1;
				if (bytes == 0) {
					state = LEADING;
				}

				break;
			}
		}
		break;
	}

	default:
		return ERROR(ASININE_ERR_INVALID, "string: unknown tag");
	}

	return ERROR(ASININE_OK, NULL);
}

// 8.23
asinine_err_t
asn1_string(const asn1_token_t *token, char *buf, size_t len) {
	RETURN_ON_ERROR(validate_string(token));

    /* DELIBERATE BUG
     * Remove the token length check and allow memcpy bugs :) 
     *
	if (len <= token->length) {
		return ERROR(ASININE_ERR_MEMORY, "string: buffer too small");
	}*/

	memcpy(buf, token->data, token->length);
	buf[token->length] = '\0';

    /* DELIBERATE BUG
     * Remove the no-NULLs check, just to confuse things :) *
     * Notice that the original author was careful to exclude this 
     * even while it is not spec-conforming.

	// We disallow NULLs in all strings, since the potential for abuse is too
	// high. This is a deviation from spec, obviously.
	if (strlen(buf) != token->length) {
		return ERROR(ASININE_ERR_INVALID, "string: contains NUL");
	}
    */

	return ERROR(ASININE_OK, NULL);
}

bool
asn1_string_eq(const asn1_token_t *token, const char *str) {
	if (validate_string(token).errno != ASININE_OK) {
		return false;
	}

	if (token->length != strlen(str)) {
		return false;
	}

	return (memcmp(token->data, str, token->length) == 0);
}

ASININE_API int
asn1_time_cmp(const asn1_time_t *a, const asn1_time_t *b) {
#define _cmp(a, b) \
	if (a != b) { \
		return (a > b) - (a < b); \
	}
	_cmp(a->year, b->year);
	_cmp(a->month, b->month);
	_cmp(a->day, b->day);
	_cmp(a->hour, b->hour);
	_cmp(a->minute, b->minute);
#undef _cmp

	return (a->minute > b->minute) - (a->minute < b->minute);
}

// 8.6
asinine_err_t
asn1_bitstring(const asn1_token_t *token, uint8_t *buf, const size_t len) {
	// Thank you http://stackoverflow.com/a/2603254
	static const uint8_t lookup[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
	    0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};

	/* First byte is number of unused bits in the last byte, must be <= 7. Last
	 * byte must not be 0, since it is not the smallest possible encoding.
	 * An empty bitstring is encoded as first byte 0 and no further data.
	 */

	// 8.6.2.2
	if (token->length == 0) {
		return ERROR(ASININE_ERR_MALFORMED, "bitstring: zero length");
	}

	if (buf != NULL && token->length - 1 > len) {
		return ERROR(ASININE_ERR_MEMORY, "bitstring: buffer too small");
	}

	memset(buf, 0, len);

	// 8.6.2.2
	uint8_t unused_bits = token->data[0];
	if (unused_bits > 7) {
		return ERROR(ASININE_ERR_MALFORMED, "bitstring: too many unused bits");
	}

	// 8.6.2.3
	if (token->length == 1) {
		if (unused_bits != 0) {
			return ERROR(
			    ASININE_ERR_MALFORMED, "bitstring: invalid zero value");
		}

		return ERROR(ASININE_OK, NULL);
	}

	// 11.2.2
	if (token->data[token->length - 1] == 0) {
		return ERROR(ASININE_ERR_MALFORMED, "bitstring: invalid padding");
	}

	// 11.2.1
	if (unused_bits > 0) {
		unused_bits = (uint8_t)((1 << unused_bits) - 1);

		if ((token->data[token->length - 1] & unused_bits) != 0) {
			return ERROR(
			    ASININE_ERR_MALFORMED, "bistring: unused bits are set");
		}
	}

	for (size_t i = 1, j = 0; i < token->length && j < len; i++, j++) {
		const uint8_t data = token->data[i];

		buf[j] = (uint8_t)(lookup[data & 0xf] << 4) | lookup[data >> 4];
	}

	return ERROR(ASININE_OK, NULL);
}

// 8.3
asinine_err_t
asn1_int(const asn1_token_t *token, asn1_word_t *value) {
	const uint8_t *data = token->data;

	if (token->length == 0) {
		return ERROR(ASININE_ERR_INVALID, "int: zero length");
	}

	if (token->length > 1) {
		// 8.3.2
		int leading = (data[0] << 1u) | (data[1] >> 7u);

		if (leading == 0 || leading == 0x1ff) {
			return ERROR(ASININE_ERR_MALFORMED, "int: not smallest encoding");
		}
	}

	if (value == NULL) {
		return ERROR(ASININE_OK, NULL);
	}

	if (token->length > sizeof *value) {
		return ERROR(ASININE_ERR_MEMORY, "int: too large");
	}

	asn1_word_t interim = 0;
	for (size_t i = 0; i < token->length; ++i) {
		// This never shifts a negative number, and is therefore not UB
		interim = (interim << 8) | data[i];
	}

	// Sign extend
	asn1_word_t mask = 1 << ((token->length * 8) - 1);
	*value           = (interim ^ mask) - mask;

	return ERROR(ASININE_OK, NULL);
}

asinine_err_t
asn1_uint_buf(const asn1_token_t *token, const uint8_t **buf, size_t *num) {
	RETURN_ON_ERROR(asn1_int(token, NULL));

	if ((token->data[0] & 0x80) != 0) {
		return ERROR(ASININE_ERR_INVALID, "uint: signed integer");
	}

	if (token->data[0] == 0) {
		// Remove padding
		*buf = token->data + 1;
		*num = token->length - 1;
		return ERROR(ASININE_OK, NULL);
	}

	*buf = token->data;
	*num = token->length;
	return ERROR(ASININE_OK, NULL);
}

static inline bool
is_leap_year(int32_t year) {
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

// static inline int
// leap_days_for_year(int32_t year)
// {
// 	return (year - 1968) / 4 - (year - 1900) / 100 + (year - 1600) / 400;
// }

static inline bool
decode_pair(const char *data, uint8_t *pair) {
	if (data[0] < 0x30 || data[0] > 0x39 || data[1] < 0x30 || data[1] > 0x39) {
		return false;
	}

	*pair = (uint8_t)((data[0] - 0x30) * 10 + (data[1] - 0x30));
	return true;
}

asinine_err_t
asn1_time(const asn1_token_t *token, asn1_time_t *time) {
	static const uint8_t days_per_month[12] = {
	    // Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec
	    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
	};
	const char *data = (char *)token->data;

	if (token->length < MIN_TIME_LENGTH || token->length > MAX_TIME_LENGTH) {
		return ERROR(ASININE_ERR_MALFORMED, "time: wrong length");
	}

	// (YY)YYMMDDHHMMSS(Z|+-D)
	uint8_t pairs[7];
	for (size_t i = 0; i < token->length / 2; data += 2, i++) {
		if (!decode_pair(data, &pairs[i])) {
			return ERROR(ASININE_ERR_MALFORMED, "time: invalid pair");
		}
	}

	// TODO: Support fractional seconds?
	// TODO: Section 11.7 specifies that time types need to
	// - truncate trailing zeros or omit fractional seconds including '.'
	// - use '.' as a fractional delimiter

	if (*data != 'Z') {
		return ERROR(ASININE_ERR_MALFORMED, "time: not in UTC");
	}

	size_t i = 0;
	switch (token->type.tag) {
	case ASN1_TAG_UTCTIME:
		time->year = pairs[0];
		i += 1;

		// Years are from (19)50 to (20)49, so 99 is 1999 and 00 is 2000.
		if (time->year > 99) {
			return ERROR(ASININE_ERR_MALFORMED, "time: invalid year");
		}

		// Normalize years, since the encoding is not linear:
		// 00 -> 2000, 49 -> 2049, 50 -> 1950, 99 -> 1999
		time->year += (time->year > 49) ? 1900 : 2000;
		break;

	case ASN1_TAG_GENERALIZEDTIME:
		time->year = pairs[0] * 100 + pairs[1];
		i += 2;

		// TODO: GeneralizedTime should not be used for dates before 2050.
		break;

	default:
		return ERROR(ASININE_ERR_MALFORMED, "time: unknown tag");
	}

	time->month  = pairs[i++];
	time->day    = pairs[i++];
	time->hour   = pairs[i++];
	time->minute = pairs[i++];
	time->second = pairs[i++];

	// Validation
	if (time->month < 1 || time->month > 12) {
		return ERROR(ASININE_ERR_MALFORMED, "time: invalid month");
	}

	if (time->day < 1) {
		return ERROR(ASININE_ERR_MALFORMED, "time: invalid day");
	} else if (is_leap_year(time->year) && time->month == 2) {
		if (time->day > 29) {
			return ERROR(ASININE_ERR_MALFORMED, "time: invalid day");
		}
	} else if (time->day > days_per_month[time->month - 1]) {
		return ERROR(ASININE_ERR_MALFORMED, "time: invalid day");
	}

	if (time->hour > 23) {
		return ERROR(ASININE_ERR_MALFORMED, "time: invalid hour");
	}

	if (time->second > 59) {
		return ERROR(ASININE_ERR_MALFORMED, "time: invalid second");
	}

	return ERROR(ASININE_OK, NULL);
}

asinine_err_t
asn1_bool(const asn1_token_t *token, bool *value) {
	uint8_t data;

	if (token->length != 1) {
		return ERROR(ASININE_ERR_MALFORMED, "bool: wrong length");
	}

	data = *token->data;
	// 11.1
	if (data == 0x00) {
		*value = false;
	} else if (data == 0xFF) {
		*value = true;
	} else {
		return ERROR(ASININE_ERR_MALFORMED, "bool: invalid value");
	}

	return ERROR(ASININE_OK, NULL);
}

asinine_err_t
asn1_null(const asn1_token_t *token) {
	if (token->length != 0) {
		return ERROR(ASININE_ERR_MALFORMED, "null: length isn't zero");
	}

	return ERROR(ASININE_OK, NULL);
}

const char *
asinine_strerror(asinine_err_t err) {
#define case_for_tag(x) \
	case x: \
		return #x
	switch (err.errno) {
		case_for_tag(ASININE_OK);
		case_for_tag(ASININE_ERR_MALFORMED);
		case_for_tag(ASININE_ERR_MEMORY);
		case_for_tag(ASININE_ERR_UNSUPPORTED);
		case_for_tag(ASININE_ERR_INVALID);
		case_for_tag(ASININE_ERR_EXPIRED);
		case_for_tag(ASININE_ERR_UNTRUSTED);
		case_for_tag(ASININE_ERR_DEPRECATED);
		case_for_tag(ASININE_ERR_NOT_FOUND);
	}
#undef case_for_tag
	return "(INVALID)";
}

static const char *
class_to_string(asn1_class_t class) {
#undef case_for
#define case_for(x) \
	case x: \
		return #x
	switch (class) {
		case_for(ASN1_CLASS_UNIVERSAL);
		case_for(ASN1_CLASS_APPLICATION);
		case_for(ASN1_CLASS_CONTEXT);
		case_for(ASN1_CLASS_PRIVATE);
	}
#undef case_for
	return "(INVALID)";
}

static const char *
tag_to_string(asn1_tag_t tag) {
#undef case_for
#define case_for(x) \
	case x: \
		return #x
	switch (tag) {
		case_for(ASN1_TAG_BOOL);
		case_for(ASN1_TAG_INT);
		case_for(ASN1_TAG_BITSTRING);
		case_for(ASN1_TAG_OCTETSTRING);
		case_for(ASN1_TAG_NULL);
		case_for(ASN1_TAG_OID);
		case_for(ASN1_TAG_UTF8STRING);
		case_for(ASN1_TAG_SEQUENCE);
		case_for(ASN1_TAG_SET);
		case_for(ASN1_TAG_PRINTABLESTRING);
		case_for(ASN1_TAG_T61STRING);
		case_for(ASN1_TAG_IA5STRING);
		case_for(ASN1_TAG_UTCTIME);
		case_for(ASN1_TAG_GENERALIZEDTIME);
		case_for(ASN1_TAG_VISIBLESTRING);
	}
#undef case_for
	return "(INVALID)";
}

size_t
asn1_type_to_string(char *dst, size_t num, const asn1_type_t *type) {
	int res;
	if (type->class == ASN1_CLASS_UNIVERSAL) {
		res = snprintf(dst, num, "%s", tag_to_string(type->tag));
	} else {
		const char *class = class_to_string(type->class);
		res               = snprintf(dst, num, "%s:%d", class, type->tag);
	}
	assert(res > 0);
	return (size_t)res;
}

size_t
asn1_time_to_string(char *dst, size_t num, const asn1_time_t *time) {
    int res =
	    snprintf(dst, num, "%04d-%02u-%02u %02u:%02u:%02u UTC", time->year,
	        time->month, time->day, time->hour, time->minute, time->second);
	assert(res > 0);
	return (size_t)res;
}

static inline bool
type_eq(const asn1_type_t *type, asn1_class_t class, asn1_tag_t tag,
    asn1_encoding_t encoding) {
	return (type->class == class) && (type->tag == tag) &&
	       (type->encoding == encoding);
}

bool
asn1_eq(const asn1_token_t *a, const asn1_token_t *b) {
	assert(a != NULL);
	assert(b != NULL);

	return (a->length == b->length) &&
	       type_eq(&a->type, b->type.class, b->type.tag, b->type.encoding) &&
	       // Since passing NULL to memcmp is UB we check for pointer equality:
	       // 1. Only length == 0 can have NULL data ptrs
	       // 2. By this point length has to be equal: either both or none NULL
	       // 3. NULL ptrs will be caught by the first case of this clause
	       ((a->data == b->data) || memcmp(a->data, b->data, a->length) == 0);
}

bool
asn1_is(const asn1_token_t *token, asn1_class_t class, asn1_tag_t tag,
    asn1_encoding_t encoding) {
	assert(token != NULL);

	return type_eq(&token->type, class, tag, encoding);
}

bool
asn1_is_time(const asn1_token_t *token) {
	assert(token != NULL);
	return type_eq(&token->type, ASN1_CLASS_UNIVERSAL, ASN1_TAG_UTCTIME,
	           ASN1_ENCODING_PRIMITIVE) ||
	       type_eq(&token->type, ASN1_CLASS_UNIVERSAL, ASN1_TAG_GENERALIZEDTIME,
	           ASN1_ENCODING_PRIMITIVE);
}

bool
asn1_is_string(const asn1_token_t *token) {
	assert(token != NULL);

	return (token->type.class == ASN1_CLASS_UNIVERSAL) &&
	       (token->type.tag == ASN1_TAG_PRINTABLESTRING ||
	           token->type.tag == ASN1_TAG_IA5STRING ||
	           token->type.tag == ASN1_TAG_UTF8STRING ||
	           token->type.tag == ASN1_TAG_VISIBLESTRING ||
	           token->type.tag == ASN1_TAG_T61STRING) &&
	       (token->type.encoding == ASN1_ENCODING_PRIMITIVE);
}

bool
asn1_is_sequence(const asn1_token_t *token) {
	return asn1_is(token, ASN1_CLASS_UNIVERSAL, ASN1_TAG_SEQUENCE,
	    ASN1_ENCODING_CONSTRUCTED);
}

bool
asn1_is_oid(const asn1_token_t *token) {
	return asn1_is(
	    token, ASN1_CLASS_UNIVERSAL, ASN1_TAG_OID, ASN1_ENCODING_PRIMITIVE);
}

bool
asn1_is_int(const asn1_token_t *token) {
	return asn1_is(
	    token, ASN1_CLASS_UNIVERSAL, ASN1_TAG_INT, ASN1_ENCODING_PRIMITIVE);
}

bool
asn1_is_bool(const asn1_token_t *token) {
	return asn1_is(
	    token, ASN1_CLASS_UNIVERSAL, ASN1_TAG_BOOL, ASN1_ENCODING_PRIMITIVE);
}

bool
asn1_is_set(const asn1_token_t *token) {
	return asn1_is(
	    token, ASN1_CLASS_UNIVERSAL, ASN1_TAG_SET, ASN1_ENCODING_CONSTRUCTED);
}

bool
asn1_is_bitstring(const asn1_token_t *token) {
	return asn1_is(token, ASN1_CLASS_UNIVERSAL, ASN1_TAG_BITSTRING,
	    ASN1_ENCODING_PRIMITIVE);
}

bool
asn1_is_octetstring(const asn1_token_t *token) {
	return asn1_is(token, ASN1_CLASS_UNIVERSAL, ASN1_TAG_OCTETSTRING,
	    ASN1_ENCODING_PRIMITIVE);
}

bool
asn1_is_null(const asn1_token_t *token) {
	return asn1_is(
	    token, ASN1_CLASS_UNIVERSAL, ASN1_TAG_NULL, ASN1_ENCODING_PRIMITIVE);
}
