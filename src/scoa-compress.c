/*
 * Copyright (C) 2013 Alexey Galakhov <agalakhov@gmail.com>
 * Copyright (C) 2020 Oleg Sazonov <whitylmn@gmail.com>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "scoa-compress.h"

#include "std.h"
#include "word.h"
#include "scoa-common.h"

#include <string.h>

struct state {

	const uint8_t *const input_buf;
	const size_t input_size;
	size_t input_pos;

	uint8_t *const output_buf;
	const size_t output_bitsize;
	unsigned bytepos;

	unsigned line_size;

	unsigned nbytes;
	uint8_t bytes[16];
};

inline void push_byte(struct state *state, uint8_t byte)
{
		state->output_buf[state->bytepos++] = byte;
}

static void swap(unsigned *a, unsigned *b)
{
	*a ^= *b;
	*b ^= *a;
	*a ^= *b;
}

static unsigned find_msb(unsigned val)
{
	/* FIXME do this faster */
	unsigned nbits;
	if (val == 0)
		return 0;
	for (nbits = 8 * sizeof(val) - 1; val < (1u << nbits); --nbits)
		;
	return nbits + 1;
}

static bool try_write_longrepeat(struct state *state)
{
	
}

static void write_simple_byte(struct state *state)
{
	unsigned i;
	uint8_t byte = state->input_buf[state->input_pos];

	push_byte(state, 0xa0 | (byte >> 3));
	push_byte(state, 0xc0 | ((byte & 0x07) << 3));
	state->input_pos += 1;
}

static bool try_write_byterepeat(struct state *state)
{
	
	uint8_t byte = state->input_buf[state->input_pos];
	unsigned short rep = 0;
	if(state->input_buf[state->input_pos+1] == byte) {
		state->input_pos++;
		for(unsigned i = 1; i <= 8; i++) {
			if(state->input_buf[state->input_pos+1] == byte) {
				rep++;
				state->input_pos++;
			} else {
				push_byte(state, byte);
				push_byte(state, 0x80 | (rep & 0x07) << 3);
				return true;
			}
		}
	}
	return false;
}

size_t scoa_compress_band(void *buf, size_t size,
	const void *band, unsigned line_size, unsigned nlines,
	enum scoa_eob_type eob_type
	/*const struct hiscoa_params *params*/)
{
	struct state state = {

		.input_buf = (const uint8_t *) band,
		.input_size = line_size * nlines,
		.input_pos = 0,

		.output_buf = (uint8_t *) buf,
		.output_bitsize = 8 * size,
		.bytepos = 0,

		.line_size = line_size,

		.nbytes = 0,
	};

	while (state.input_pos < state.input_size) {
		//if (try_write_longrepeat(&state))
		//	continue;
		if (try_write_byterepeat(&state))
			continue;
		write_simple_byte(&state);
	}

	push_byte(&state, 0xFE); /* end */
	push_byte(&state, (unsigned) eob_type);
	//if (state.bitpos % 32)
	//	push_bits(&state, 0xFFFFFFFF, 32 - (state.bitpos % 32));

	return state.bytepos;
}