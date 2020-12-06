/*
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

 #include "std.h"
#include "word.h"
#include "capt-command.h"
#include "capt-status.h"
#include "generic-ops.h"
#include "scoa-common.h"
#include "scoa-compress.h"
#include "paper.h"
#include "printer.h"
#include "magic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint16_t job;

struct lbp1120_ops_s {
	struct printer_ops_s ops;

	const struct capt_status_s * (*get_status) (void);
	void (*wait_ready) (void);
};

static const struct capt_status_s *lbp1120_get_status(const struct printer_ops_s *ops)
{
	const struct lbp1120_ops_s *lops = container_of(ops, struct lbp1120_ops_s, ops);
	return lops->get_status();
}

static void lbp1120_wait_ready(const struct printer_ops_s *ops)
{
	const struct lbp1120_ops_s *lops = container_of(ops, struct lbp1120_ops_s, ops);
	lops->wait_ready();
}

static void send_job_start(uint8_t fg, uint16_t page)
{
	uint8_t ml = 0x00; /* host name lenght */
	uint8_t ul = 0x00; /* user name lenght */
	uint8_t nl = 0x00; /* document name lenght */
	time_t rawtime = time(NULL);
	const struct tm *tm = localtime(&rawtime);
	uint8_t buf[32 + 40 + ml + ul + nl];
	uint8_t head[32] = {
		0x00, 0x00, 0x00, 0x00, LO(page), HI(page), 0x00, 0x00,
		ml, 0x00, ul, 0x00, nl, 0x00, 0x00, 0x00,
		fg, 0x01, LO(job), HI(job),
		/*-60 */ 0xC4, 0xFF,
		/*-120*/ 0x88, 0xFF,
		LO(tm->tm_year), HI(tm->tm_year), (uint8_t) tm->tm_mon, (uint8_t) tm->tm_mday,
		(uint8_t) tm->tm_hour, (uint8_t) tm->tm_min, (uint8_t) tm->tm_sec,
		0x01,
	};
	memcpy(buf, head, sizeof(head));
	memset(buf + 32, 0, 40 + ml + ul + nl);
	capt_sendrecv(CAPT_JOB_SETUP, buf, sizeof(buf), NULL, 0);
}

static void lbp1120_job_prologue(struct printer_state_s *state)
{
	(void) state;
	uint8_t buf[8];
	size_t size;

	capt_sendrecv(CAPT_IDENT, NULL, 0, NULL, 0);
	sleep(1);
	capt_init_status();
	lbp1120_get_status(state->ops);

	capt_sendrecv(CAPT_START_0, NULL, 0, NULL, 0);
	capt_sendrecv(CAPT_JOB_BEGIN, magicbuf_0, ARRAY_SIZE(magicbuf_0), buf, &size);
	job=WORD(buf[2], buf[3]);

	lbp1120_wait_ready(state->ops);

	send_job_start(1, 0);
	lbp1120_wait_ready(state->ops);
}
static bool lbp1120_page_prologue(struct printer_state_s *state, const struct page_dims_s *dims)
{
	const struct capt_status_s *status;
	size_t s;
	uint8_t buf[16];

	uint8_t save = dims->toner_save;
	uint8_t fm = 0x00; /* fuser mode (temperature?) */

	switch (dims->media_type) {
		case 0x00:
		case 0x02:
			/* Plain Paper & Plain Paper L */
			fm = 0x01;
			break;
		case 0x01:
			/* Heavy Paper */
			fm = 0x01;
			break;
		case 0x03:
			/* Heavy Paper H */
			fm = 0x02;
			break;
		case 0x04:
			/* Transparency */
			fm = 0x13;
			break;
		case 0x05:
			/* Envelope */
			fm = 0x1C;
			break;
		default:
			fm = 0x01;
	}

	fprintf(stderr, "DEBUG: CAPT: media_type=%u, fm=%u\n", dims->media_type, fm);

	uint8_t pageparms[] = {
		/* Bytes 0-21 (0x00 to 0x15) */
		0x00, 0x00, 0x30, 0x2A, /* sz */ 0x02, 0x00, 0x00, 0x00,
		0x1C, 0x1C, 0x1C, 0x1C, dims->media_type, /* adapt */ 0x11, 0x04, 0x00,
		0x01, 0x01, /* img ref */ 0x00, save, 0x00, 0x00,
		/* Bytes 22-33 (0x16 to 0x21) */
		LO(dims->margin_height), HI(dims->margin_height),
		LO(dims->margin_width), HI(dims->margin_width),
		LO(dims->line_size), HI(dims->line_size),
		LO(dims->num_lines), HI(dims->num_lines),
		LO(dims->paper_width), HI(dims->paper_width),
		LO(dims->paper_height), HI(dims->paper_height),
		/* Bytes 34-39 (0x22 to 0x27) */
		0x00, 0x00, fm, 0x00, 0x00, 0x00,
		/* Spare bytes for later
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		*/
	};

	(void) state;

	status = lbp1120_get_status(state->ops);
	if (FLAG(status, CAPT_FL_UNINIT1) || FLAG(status, CAPT_FL_UNINIT2)) {
		capt_sendrecv(CAPT_START_1, NULL, 0, NULL, 0);
		capt_sendrecv(CAPT_START_2, NULL, 0, NULL, 0);
		capt_sendrecv(CAPT_START_3, NULL, 0, NULL, 0);

		/* FIXME: wait for printer is free (could it be potentially dangerous or really mandatory?) */
		while ( ! FLAG(lbp1120_get_status(state->ops), ((4 << 16) | (1 << 0)) ) )
		  sleep(1);
		lbp1120_get_status(state->ops);


		lbp1120_wait_ready(state->ops);
		capt_sendrecv(CAPT_UPLOAD_2, magicbuf_2, ARRAY_SIZE(magicbuf_2), NULL, 0);
		lbp1120_wait_ready(state->ops);
	}

	while (1) {
		if (! FLAG(lbp1120_get_status(state->ops), CAPT_FL_BUFFERFULL))
			break;
		sleep(1);
	}

	capt_multi_begin(CAPT_SET_PARMS);
	capt_multi_add(CAPT_SET_PARM_PAGE, pageparms, sizeof(pageparms));
	capt_multi_add(CAPT_SET_PARM_1, NULL, 0);
	capt_multi_add(CAPT_SET_PARM_2, NULL, 0);
	capt_multi_send();

	return true;
}

static bool lbp1120_page_epilogue(struct printer_state_s *state, const struct page_dims_s *dims)
{
	(void) dims;
	const struct capt_status_s *status;

	capt_send(CAPT_PRINT_DATA_END, NULL, 0);

	/* waiting until the page is received */
	while (1) {
	  sleep(1);
	  status = lbp1120_get_status(state->ops);
	  if (status->page_received == status->page_decoding)
	    break;
	}
	send_job_start(2, status->page_decoding);
	lbp1120_wait_ready(state->ops);

	uint8_t buf[2] = { LO(status->page_decoding), HI(status->page_decoding) };
	capt_sendrecv(CAPT_FIRE, buf, 2, NULL, 0);
	lbp1120_wait_ready(state->ops);

	send_job_start(6, status->page_decoding);

	while (1) {
		const struct capt_status_s *status = lbp1120_get_status(state->ops);
		/* Interesting. Using page_printing here results in shifted print */
		if (status->page_out == status->page_decoding)
			return true;
		if (FLAG(status, CAPT_FL_NOPAPER2) || FLAG(status, CAPT_FL_NOPAPER1)) {
			fprintf(stderr, "DEBUG: CAPT: no paper\n");
			if (FLAG(status, CAPT_FL_PRINTING) || FLAG(status, CAPT_FL_PROCESSING1))
				continue;
			return false;
		}
		sleep(1);
	}
}

static void lbp1120_page_setup(struct printer_state_s *state,
		struct page_dims_s *dims,
		unsigned width, unsigned height)
{ 
	/* FIXME: Do we still need this function? */
	(void) state;
	(void) width;
	(void) height;
	/* Get raster dimensions straight from CUPS in paper.c */
	dims->num_lines = dims->paper_height;
	dims->line_size = dims->paper_width / 8;
	dims->band_size = 70;
}

static void lbp1120_wait_user(struct printer_state_s *state)
{
	(void) state;

	capt_sendrecv(CAPT_GPIO, blinkonbuf, ARRAY_SIZE(blinkonbuf), NULL, 0);
	lbp1120_wait_ready(state->ops);

	while (1) {
		const struct capt_status_s *status = lbp1120_get_status(state->ops);
		if (FLAG(status, CAPT_FL_BUTTON)) {
			fprintf(stderr, "DEBUG: CAPT: button pressed\n");
//			break;
		}
		if (FLAG(status, CAPT_FL_nERROR)) {
			fprintf(stderr, "DEBUG: CAPT: virtual button pressed\n");
			break;
		}
		sleep(1);
	}

	capt_sendrecv(CAPT_GPIO, blinkoffbuf, ARRAY_SIZE(blinkoffbuf), NULL, 0);
	lbp1120_wait_ready(state->ops);
}
static void lbp1120_job_epilogue(struct printer_state_s *state)
{
	uint8_t jbuf[2] = { LO(job), HI(job) };

	while (1) {
		const struct capt_status_s *status = lbp1120_get_status(state->ops);
		if (status->page_completed == status->page_decoding)
			break;
		sleep(1);
	}
	capt_sendrecv(CAPT_JOB_END, jbuf, 2, NULL, 0);
}
static struct lbp1120_ops_s lbp1120_ops = {
	.ops = {
		.job_prologue = lbp1120_job_prologue,
		.job_epilogue = lbp1120_job_epilogue,
		.page_setup = lbp1120_page_setup,
		.page_prologue = lbp1120_page_prologue,
		.page_epilogue = lbp1120_page_epilogue,
		.compress_band = ops_compress_band_scoa,
		.send_band = ops_send_band_scoa,
		.wait_user = lbp1120_wait_user,
	},
	.get_status = capt_get_xstatus_only,
	.wait_ready = capt_wait_xready_only,
};

register_printer("LBP1120", lbp1120_ops.ops, BROKEN); // NOW BROKEN