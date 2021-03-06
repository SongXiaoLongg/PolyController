/*
 * Copyright (c) 2004, Adam Dunkels.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: httpd.c,v 1.20 2010-12-14 22:45:22 dak664 Exp $
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>
#include <ctype.h>

#include "contiki-net.h"

#include "webserver.h"
#include "http-strings.h"
#include "urlconv.h"

#include "httpd.h"

#ifndef CONFIG_APPS_WEBSERVER_CONNS
#define CONNS UIP_CONNS
#else /* CONFIG_APPS_WEBSERVER_CONNS */
#define CONNS CONFIG_APPS_WEBSERVER_CONNS
#endif /* CONFIG_APPS_WEBSERVER_CONNS */

#define SEND_PSTR(sock, str) \
	PSOCK_GENERATOR_SEND(sock, send_pstr_gen, (void *)str)

static uint8_t conns_free = CONNS;

static unsigned short send_pstr_gen(void *string) {
	PGM_P str = string;

	// Find out how long the string is
	int len = strlen_P(str);

	// Make sure it's not a stupid length
	if (len > UIP_TCP_MSS) {
		len = UIP_TCP_MSS;
	}

	// Copy the string into the send buffer
	memcpy_P(uip_appdata, str, len);

	return len;
}

static PT_THREAD(send_pstring(struct httpd_state *s, PGM_P str)) {
	PSOCK_BEGIN(&s->sock);
	SEND_PSTR(&s->sock, str);
	PSOCK_END(&s->sock);
}

static PT_THREAD(send_headers(struct httpd_state *s, const PGM_P statushdr)) {
	const char *ptr = NULL;
	const PGM_P ptr2 = NULL;

	PSOCK_BEGIN(&s->sock);
	SEND_PSTR(&s->sock, statushdr);

	ptr = strrchr(s->filename, '.');
	if (ptr == NULL) {
		ptr2 = http_content_type_binary;
	}
	else if (strncmp_P(ptr, http_html, 5) == 0) {
		ptr2 = http_content_type_html;
	}
	else if (strncmp_P(ptr, http_shtml, 6) == 0) {
		ptr2 = http_content_type_html;
	}
	else if (strncmp_P(ptr, http_css, 4) == 0) {
		ptr2 = http_content_type_css;
	}
	else if (strncmp_P(ptr, http_png, 4) == 0) {
		ptr2 = http_content_type_png;
	}
	else if (strncmp_P(ptr, http_gif, 4) == 0) {
		ptr2 = http_content_type_gif;
	}
	else if (strncmp_P(ptr, http_jpg, 4) == 0) {
		ptr2 = http_content_type_jpg;
	}
	else {
		ptr2 = http_content_type_plain;
	}

	SEND_PSTR(&s->sock, ptr2);
	PSOCK_END(&s->sock);
}

static PT_THREAD(handle_input(struct httpd_state *s)) {
	PSOCK_BEGIN(&s->sock);

	// Read the request method
	PSOCK_READTO(&s->sock, ' ');

	// Work out the request method
	if (strncmp_P((char *)s->inputbuf, http_get, 4) == 0) {
		s->method = HTTPD_METHOD_GET;
	}
	else if (strncmp_P((char *)s->inputbuf, http_post, 4) == 0) {
		s->method = HTTPD_METHOD_POST;
	}
	else {
		s->method = HTTPD_METHOD_INVALID;
	}

	// Get the query string
	PSOCK_READTO(&s->sock, ' ');

	// null-terminate the path string
	s->inputbuf[PSOCK_DATALEN(&s->sock) - 1] = 0;

	// Make sure it starts with a '/'
	if (s->inputbuf[0] == '/') {
		// prefix the path with '/www'
		strcpy_P(s->filename, PSTR("/www"));

		// Use urlconv to sanitise the path
		int idx = strlen(s->filename);
		urlconv_tofilename(&s->filename[idx], (char *)s->inputbuf,
			sizeof(s->filename) - idx);

		// Append 'index.html' if necessary
		idx = strlen(s->filename);
		if (s->filename[idx - 1] == '/' &&
			idx <= sizeof(s->filename) - sizeof(http_index_html))
		{
			strcpy_P(&s->filename[idx - 1], http_index_html);
		}

		webserver_log_file(&uip_conn->ripaddr, s->filename);
	}
	else {
		// Invalid path
		s->filename[0] = 0;
	}

	// Skip past the HTTP protocol version
	PSOCK_READTO(&s->sock, '\n');

	while (1) {
		PSOCK_READTO(&s->sock, '\n');

		int len = PSOCK_DATALEN(&s->sock);

		// Trim trailing whitespace
		while (isspace(s->inputbuf[len - 1])) {
			s->inputbuf[len - 1] = 0;
			len--;
		}

		// Empty line means end of headers
		if (len == 0) {
			break;
		}
	}

	PSOCK_END(&s->sock);
}

static PT_THREAD(handle_connection(struct httpd_state *s)) {
	PT_BEGIN(&s->pt);

	// Read the request
	PT_WAIT_THREAD(&s->pt, handle_input(s));

	if ((s->method == HTTPD_METHOD_INVALID) ||
		(s->filename[0] == 0))
	{
		// Bad request
		PT_WAIT_THREAD(&s->pt, send_pstring(s, http_header_400));
	}
	else if (s->method == HTTPD_METHOD_GET) {
		// Default sendfile flags
		uint8_t flags = SENDFILE_MODE_NORMAL;

		// Work out if it's a script or not
		char *ptr = strrchr(s->filename, '.');
		if (ptr != NULL && strncmp_P(ptr, http_shtml, sizeof(http_shtml)) == 0) {
			flags = SENDFILE_MODE_SCRIPT;
		}

		// Init sendfile
		int ret = sendfile_init(&s->sendfile, s->filename, flags);
		if (ret < 0) {
			// Replace the filename as send_headers guesses the content type
			strcpy_P(s->filename, PSTR("/notfound.html"));

			// Open failed, so send a 404 header
			PT_WAIT_THREAD(&s->pt, send_headers(s, http_header_404));

			// Open the 404 notfound.html file
			ret = sendfile_init(&s->sendfile, s->filename,
				SENDFILE_MODE_NORMAL);
			if (ret < 0) {
				// We couldn't open the notfound.html file
				webserver_log_file(&uip_conn->ripaddr,
					"404 (no notfound.html)");

				PT_WAIT_THREAD(&s->pt,
					send_pstring(s, PSTR("Error 404: resource not found")));

				PSOCK_CLOSE(&s->sock);
				PT_EXIT(&s->pt);
			}

			webserver_log_file(&uip_conn->ripaddr, "404 /notfound.html");
		}
		else {
			PT_WAIT_THREAD(&s->pt, send_headers(s, http_header_200));
		}

		// Do the work of sending the file (or script)
		PT_WAIT_THREAD(&s->pt, sendfile(&s->sendfile, s));

		// Free sendfile memory
		sendfile_finish(&s->sendfile);
	}
	else {
		// Bad request (we don't do POST yet)
		PT_WAIT_THREAD(&s->pt, send_pstring(s, http_header_400));
	}

	// Close the socket & finish up
	PSOCK_CLOSE(&s->sock);
	PT_END(&s->pt);
}

void httpd_appcall(void *state) {
	struct httpd_state *s = (struct httpd_state *)state;

	if (uip_closed() || uip_aborted() || uip_timedout()) {
		if (s != NULL) {
			// Make sure sendfile is cleaned up
			sendfile_finish(&s->sendfile);

			// Free state data
			free(s);
			conns_free++;
			tcp_markconn(uip_conn, NULL);
		}
	}
	else if (uip_connected()) {
		// Allocate a connection if we can
		if (conns_free) {
			s = calloc(1, sizeof(*s));
			conns_free--;
		}
		else {
			s = NULL;
		}

		// Make sure we got some memory
		if (s == NULL) {
			uip_abort();
			webserver_log_file(&uip_conn->ripaddr, "503 Out of memory");
			return;
		}

		// Set up the connection
		tcp_markconn(uip_conn, s);
		PSOCK_INIT(&s->sock, (uint8_t *)s->inputbuf, sizeof(s->inputbuf) - 1);
		PT_INIT(&s->pt);
		timer_set(&s->timer, CLOCK_SECOND * 10);
		handle_connection(s);
	}
	else if (s != NULL) {
		if (uip_poll()) {
			if (timer_expired(&s->timer)) {
				uip_abort();

				// Make sure sendfile is cleaned up
				sendfile_finish(&s->sendfile);

				// Free state data
				free(s);
				s = NULL;
				tcp_markconn(uip_conn, NULL);
				conns_free++;

				webserver_log_file(&uip_conn->ripaddr, "408 Connection reset");
			}
		}
		else {
			timer_restart(&s->timer);
		}

		if (s) {
			handle_connection(s);
		}
	}
	else {
		uip_abort();
	}
}

void httpd_init(void) {
	tcp_listen(UIP_HTONS(80));
}

#if UIP_CONF_IPV6
uint8_t httpd_sprint_ip6(uip_ip6addr_t addr, char * result) {
	unsigned char i = 0;
	unsigned char zerocnt = 0;
	unsigned char numprinted = 0;
	char * starting = result;

	*result++='[';
	while (numprinted < 8) {
		if ((addr.u16[i] == 0) && (zerocnt == 0)) {
			while(addr.u16[zerocnt + i] == 0) zerocnt++;

			if (zerocnt == 1) {
				*result++ = '0';
				numprinted++;
				break;
			}

			i += zerocnt;
			numprinted += zerocnt;
		}
		else {
			result += sprintf(result, "%x",
				(unsigned int)(uip_ntohs(addr.u16[i])));
			i++;
			numprinted++;
		}
		if (numprinted != 8) *result++ = ':';
	}

	*result++=']';
	*result=0;
	return (result - starting);
}
#endif /* UIP_CONF_IPV6 */

