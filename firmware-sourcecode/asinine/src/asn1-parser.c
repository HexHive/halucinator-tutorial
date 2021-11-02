/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <assert.h>
#include <string.h>

#include "asinine/asn1.h"
#include "asinine/errors.h"
#include "internal/macros.h"

#if ASN1_MAXIMUM_DEPTH > UINT8_MAX
#error Maximum ASN.1 depth must be smaller than UINT8_MAX
#endif

// X.690 11/2008 item 8.1.2.4.1
#define TYPE_MULTIPART_TAG (31)

#define TYPE_CLASS(x) (((x) & (3 << 6)) >> 6)
#define TYPE_ENCODING(x) (((x) & (1 << 5)) >> 5)
#define TYPE_TAG(x) ((x) & ((1 << 5) - 1))

#define MULTIPART_TAG_BITS_PER_BYTE (7)
#define MULTIPART_TAG_MASK ((1 << 7) - 1)
#define MULTIPART_TAG_CONTINUATION (1 << 7)

// X.690 11/2008 item 8.1.3.5 (a)
#define CONTENT_LENGTH_LONG_MASK (1 << 7)
#define CONTENT_LENGTH_MASK ((1 << 7) - 1)
// X.690 11/2008 item 8.1.3.5 (c)
#define CONTENT_LENGTH_LONG_RESERVED ((1 << 7) - 1)
#define CONTENT_LENGTH_LONG_MIN (128)

#define CONTENT_LENGTH_IS_LONG_FORM(x) ((x)&CONTENT_LENGTH_LONG_MASK)

void
asn1_init(asn1_parser_t *parser, const uint8_t *data, size_t length) {
	assert(parser != NULL);
	assert(data != NULL);

	*parser         = (asn1_parser_t){0};
	parser->current = data;
	parser->end     = data + length;
}

void
asn1_unsafe_skip(asn1_parser_t *parser) {
	parser->current = parser->end;
}

bool
asn1_eof(const asn1_parser_t *parser) {
	return parser->current == parser->end;
}

bool
asn1_end(const asn1_parser_t *parser) {
	return asn1_eof(parser) && parser->depth == 0;
}

static inline bool
advance_pos(asn1_parser_t *parser, size_t num) {
	// num is under attacker control
	if ((size_t)((const uint8_t *)parser->end - parser->current) <= num) {
		return false;
	}

	parser->current += num;
	return true;
}

asinine_err_t
asn1_next(asn1_parser_t *parser) {
	asn1_token_t *const token = &parser->token;

	if (parser->current >= (const uint8_t *)parser->end) {
		return ERROR(ASININE_ERR_MALFORMED, "EOF");
	}

	*token       = (asn1_token_t){0};
	token->start = parser->current;

	// Type (8.1.2)
	token->type.class    = TYPE_CLASS(*parser->current);
	token->type.encoding = TYPE_ENCODING(*parser->current);
	token->type.tag      = TYPE_TAG(*parser->current);

	if (token->type.tag == TYPE_MULTIPART_TAG) {
		size_t bits;

		// 8.1.2.4.2
		bits            = 0;
		token->type.tag = 0;

		do {
			if (!advance_pos(parser, 1)) {
				return ERROR(ASININE_ERR_MALFORMED, "token: tag: truncated");
			}

			token->type.tag <<= MULTIPART_TAG_BITS_PER_BYTE;
			token->type.tag |= *parser->current & MULTIPART_TAG_MASK;

			// TODO: Could this overflow bits?
			bits += MULTIPART_TAG_BITS_PER_BYTE;
			if (bits > ASN1_TYPE_TAG_BITS) {
				return ERROR(ASININE_ERR_MEMORY, "token: tag: too large");
			}
		} while (*parser->current & MULTIPART_TAG_CONTINUATION);
	}

	// Length (8.1.3)
	if (!advance_pos(parser, 1)) {
		return ERROR(ASININE_ERR_MALFORMED, "token: truncated header");
	}

	if (CONTENT_LENGTH_IS_LONG_FORM(*parser->current)) {
		size_t i, num_bytes;

		num_bytes = *parser->current & CONTENT_LENGTH_MASK;

		if (num_bytes == CONTENT_LENGTH_LONG_RESERVED) {
			return ERROR(ASININE_ERR_MALFORMED, "token: length: reserved");
		} else if (num_bytes == 0) {
			// Indefinite form is forbidden (X.690 11/2008 8.1.3.6)
			return ERROR(ASININE_ERR_MALFORMED, "token: length: indefinite");
		} else if (num_bytes > sizeof token->length) {
			return ERROR(ASININE_ERR_UNSUPPORTED, "token: length: too long");
		}

		for (i = 0; i < num_bytes; i++) {
			if (!advance_pos(parser, 1)) {
				return ERROR(ASININE_ERR_MALFORMED, "token: length: truncated");
			}

			if (token->length == 0 && *parser->current == 0) {
				return ERROR(
				    ASININE_ERR_MALFORMED, "token: lenght: leading zero");
			}

			token->length = (token->length << 8) | *parser->current;
		}

		// 10.1
		if (token->length < CONTENT_LENGTH_LONG_MIN) {
			return ERROR(ASININE_ERR_MALFORMED,
			    "token: length: below long-form minimum");
		}
	} else {
		token->length = *parser->current & CONTENT_LENGTH_MASK;
	}

	// Content and overflow check
	if (token->length > 0) {
		const uint8_t *data = parser->current + 1;

		if (!advance_pos(parser, token->length)) {
			return ERROR(ASININE_ERR_MALFORMED, "token: truncated content");
		}

		token->data = data;
	}

	// Token has been successfully parsed, we now step past the current token.
	// The error condition for this is checked on the next call of this
	// function.
	parser->current++;
	return ERROR(ASININE_OK, NULL);
}

asinine_err_t
asn1_abort(asn1_parser_t *parser) {
	if (parser->depth == 0) {
		return ERROR(ASININE_ERR_INVALID, "abort: already at root");
	}

	const uint8_t *end   = parser->stack[0];
	const uint8_t *start = parser->end;
	if (parser->depth > 1) {
		start = parser->stack[1];
	}

	size_t length = (size_t)(end - start);
	asn1_init(parser, start, length);
	return ERROR(ASININE_OK, NULL);
}

asinine_err_t
asn1_push(asn1_parser_t *parser) {
	if (parser->token.type.encoding != ASN1_ENCODING_CONSTRUCTED) {
		return ERROR(ASININE_ERR_INVALID, "push: not constructed");
	}

	return asn1_force_push(parser);
}

asinine_err_t
asn1_force_push(asn1_parser_t *parser) {
	const asn1_token_t *token = &parser->token;

	if (parser->depth + 1 > NUM(parser->stack)) {
		return ERROR(ASININE_ERR_UNSUPPORTED, "push: nested too deep");
	}

	parser->stack[parser->depth] = parser->end;
	parser->depth++;

	if (token->data == NULL) {
		// Empty tokens are valid, we just have to make sure that eof returns
		// true for them.
		parser->end = parser->current;
		return ERROR(ASININE_OK, NULL);
	}

	if (asn1_is_bitstring(token)) {
		// Bitstrings have a pesky leading byte which indicates how many
		// bits in the last data byte are unused. This isn't part of the
		// encoded data and needs to be skipped.
		RETURN_ON_ERROR(asn1_bitstring(token, NULL, 0));
		parser->current = token->data + 1;
	} else {
		// We've already skipped to the end of the token, so reset the current
		// position to the start of the token's data.
		parser->current = token->data;
	}
	parser->end = token->data + token->length;

	return ERROR(ASININE_OK, NULL);
}

asinine_err_t
asn1_pop(asn1_parser_t *parser) {
	if (parser->depth == 0) {
		return ERROR(ASININE_ERR_INVALID, "pop: already at root");
	}

	// Don't pop tokens which havent been fully parsed
	if (!asn1_eof(parser)) {
		return ERROR(ASININE_ERR_MALFORMED, "pop: parent not fully parsed");
	}

	parser->depth--;
	parser->end                  = parser->stack[parser->depth];
	parser->stack[parser->depth] = NULL;
	return ERROR(ASININE_OK, NULL);
}

asinine_err_t
asn1_tokens(asn1_parser_t *parser, void *ctx,
    void (*fn)(const asn1_token_t *, uint8_t depth, void *ctx)) {
	while (!asn1_end(parser)) {
		RETURN_ON_ERROR(asn1_next(parser));

		fn(&parser->token, parser->depth, ctx);

		if (parser->token.type.encoding == ASN1_ENCODING_CONSTRUCTED) {
			RETURN_ON_ERROR(asn1_push(parser));
		}

		while (asn1_eof(parser)) {
			RETURN_ON_ERROR(asn1_pop(parser));
		}
	}

	return ERROR(ASININE_OK, NULL);
}

asinine_err_t
asn1_push_seq(asn1_parser_t *parser) {
	RETURN_ON_ERROR(asn1_next(parser));

	if (!asn1_is_sequence(&parser->token)) {
		return ERROR(ASININE_ERR_INVALID, "push: token not a sequence");
	}

	return asn1_push(parser);
}
