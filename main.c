#include <error.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <uriparser/Uri.h>
#include <time.h>
#include <pulse/simple.h>

#include "timespec.h"
#include "ohz_v1.h"
#include "ohm_v1.h"

void open_uri(char *uri_string);

char *resolve_preset(int preset) {
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (fd <= 0)
		error(1, errno, "Could not open socket");

	struct sockaddr_in src = {
		.sin_family = AF_INET,
		.sin_port = htons(51972),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
		error(1, 0, "setsockopt(SO_REUSEADDR) failed");

	if (bind(fd, (struct sockaddr *) &src, sizeof(src)) < 0)
		error(1, 0, "Could not bind socket");

	struct ip_mreq mreq = {
		mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250")
	};

	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
		error(1, 0, "Could not join multicast group");

	struct sockaddr_in dst = {
		.sin_family = AF_INET,
		.sin_port = htons(51972),
		.sin_addr.s_addr = inet_addr("239.255.255.250")
	};

	ohz1_preset_query query = {
		.hdr = {
			.signature = "Ohz ",
			.version = 1,
			.type = OHZ1_PRESET_QUERY,
			.length = htons(sizeof(ohz1_preset_query))
		},
		.preset = htonl(preset)
	};

	xmlDocPtr metadata = NULL;

	while (1) {
		if (sendto(fd, &query, sizeof(query), 0, (const struct sockaddr*) &dst, sizeof(dst)) < 0)
			error(1, errno, "sendto failed");

		uint8_t buf[4096];

		struct timeval timeout = {
			.tv_usec = 100000
		};

		if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
			error(1, errno, "setsockopt(SO_RCVTIMEO) failed");

		while (1) {
			ssize_t n = recv(fd, buf, sizeof(buf), 0);

			if (n < 0) {
				if (errno == EAGAIN)
					break;

				error(1, errno, "recv");
			}

			if (n < sizeof(ohz1_preset_info))
				continue;

			ohz1_preset_info *info = (void *)buf;

			if (strncmp(info->hdr.signature, "Ohz ", 4) != 0)
				continue;

			if (info->hdr.version != 1)
				continue;

			if (info->hdr.type != OHZ1_PRESET_INFO)
				continue;

			if (htonl(info->preset) != preset)
				continue;

			metadata = xmlReadMemory(info->metadata, htonl(info->length), "noname.xml", NULL, 0);
			if (metadata == NULL)
				error(1, 0, "Could not parse metadata");

			break;
		}

		if (metadata != NULL)
			break;

	}

	xmlXPathContextPtr context;
	xmlXPathObjectPtr result;

	context = xmlXPathNewContext(metadata);
	result = xmlXPathEvalExpression("//*[local-name()='res']/text()", context);
	xmlXPathFreeContext(context);
	if (result == NULL || xmlXPathNodeSetIsEmpty(result->nodesetval))
		error(1, 0, "Could not find URI in metadata");

	xmlChar *uri = xmlXPathCastToString(result);
	char *s = strdup(uri);
	xmlFree(uri);
	xmlCleanupParser();

	return s;
}

char *UriTextRangeString(UriTextRangeA *textrange) {
	return strndup(textrange->first, textrange->afterLast - textrange->first);
}

struct uri {
	char *scheme;
	char *host;
	int port;
	char *path;
};

struct uri *parse_uri(char *uri_string) {
	UriParserStateA state;
	UriUriA uri;

	state.uri = &uri;

	if (uriParseUriA(&state, uri_string) != URI_SUCCESS) {
		uriFreeUriMembersA(&uri);
		error(1, 0, "Could not parse URI");
	}

	struct uri *s = calloc(1, sizeof(struct uri));

	s->scheme = UriTextRangeString(&uri.scheme);
	s->host = UriTextRangeString(&uri.hostText);
	s->port = atoi(UriTextRangeString(&uri.portText));

	if (&uri.pathHead->text != NULL)
		s->path = UriTextRangeString(&uri.pathHead->text);
	else
		s->path = strdup("");

        uriFreeUriMembersA(&uri);

	return s;
}

void free_uri(struct uri *uri) {
	free(uri->scheme);
	free(uri->host);
	free(uri->path);
	free(uri);
}

void resolve_ohz(struct uri *uri) {
	printf("Resolving OHZ\n");
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (fd <= 0)
		error(1, errno, "Could not open socket");

	struct sockaddr_in src = {
		.sin_family = AF_INET,
		.sin_port = htons(51972),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
		error(1, 0, "setsockopt(SO_REUSEADDR) failed");

	if (bind(fd, (struct sockaddr *) &src, sizeof(src)) < 0)
		error(1, 0, "Could not bind socket");

	struct ip_mreq mreq = {
		mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250")
	};

	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
		error(1, 0, "Could not join multicast group");

	struct sockaddr_in dst = {
		.sin_family = AF_INET,
		.sin_port = htons(uri->port),
		.sin_addr.s_addr = inet_addr(uri->host)
	};

	ohz1_zone_query query = {
		.hdr = {
			.signature = "Ohz ",
			.version = 1,
			.type = OHZ1_ZONE_QUERY,
			.length = htons(sizeof(ohz1_preset_query))
		},
		.zone_length = htonl(strlen(uri->path))
	};

  size_t query_size = sizeof(ohz1_zone_query) + strlen(uri->path);
	uint8_t *query_with_id = malloc(query_size);

	struct iovec iov[] = {
		{
			.iov_base = &query,
			.iov_len = sizeof(query)
		},
		{
			.iov_base = uri->path,
			.iov_len = strlen(uri->path)
		}
	};

	struct msghdr message = {
		.msg_name = &dst,
		.msg_namelen = sizeof(dst),
		.msg_iov = iov,
		.msg_iovlen = 2
	};

	char *zone_uri = NULL;

	while (1) {
		if (sendmsg(fd, &message, 0) == -1)
			error(1, errno, "sendmsg failed");

		uint8_t buf[4096];

		struct timeval timeout = {
			.tv_usec = 100000
		};

		if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
			error(1, errno, "setsockopt(SO_RCVTIMEO) failed");

		while (1) {
			ssize_t n = recv(fd, buf, sizeof(buf), 0);

			if (n < 0) {
				if (errno == EAGAIN)
					break;

				error(1, errno, "recv");
			}

			if (n < sizeof(ohz1_zone_uri))
				continue;

			ohz1_zone_uri *info = (void *)buf;

			if (strncmp(info->hdr.signature, "Ohz ", 4) != 0)
				continue;

			if (info->hdr.version != 1)
				continue;

			if (info->hdr.type != OHZ1_ZONE_URI)
				continue;

			size_t zone_length = htonl(info->zone_length);
			size_t uri_length = htonl(info->uri_length);

			if (strlen(uri->path) != zone_length || strncmp(uri->path, info->data, zone_length) != 0)
				continue;

			zone_uri = strndup(info->data + zone_length, uri_length);

			break;
		}

		if (zone_uri != NULL)
			break;
	}

	open_uri(zone_uri);

	free(zone_uri);

	// de-dup socket code with presety_y_ foo
}

int latency_to_ms(int samplerate, int latency) {
	int multiplier = (samplerate%441) == 0 ? 44100 : 48000;
	return (unsigned long long int)latency * 1000 / (256 * multiplier);
}

void play_frame(ohm1_audio *frame) {
	static pa_simple *pa_stream = NULL;
	static pa_sample_spec current_ss;
	static int last_frame = 0;

	pa_sample_spec ss = {
		.rate = htonl(frame->samplerate),
		.channels = frame->channels
	};

	int this_frame = htonl(frame->frame);
	int frame_delta = this_frame - last_frame;
	last_frame = this_frame;

	if (frame_delta < 1)
		return;

	switch (frame->bitdepth) {
		case 24:
			ss.format = PA_SAMPLE_S24BE;
			break;
		case 16:
			ss.format = PA_SAMPLE_S16BE;
			break;
		default:
			error(1, 0, "Unsupported bit depth %i\n", frame->bitdepth);
	}

	int latency = latency_to_ms(ss.rate, htonl(frame->media_latency));
	size_t framesize = pa_frame_size(&ss);

	pa_buffer_attr bufattr = {
		.maxlength = -1,
		.minreq = -1,
		.prebuf = -1,
		.tlength = 2 * (latency * framesize) * (ss.rate / 1000)
	};

	if ((!pa_sample_spec_equal(&current_ss, &ss)) && pa_stream != NULL) {
		printf("Draining.\n");
		pa_simple_drain(pa_stream, NULL);
		pa_simple_free(pa_stream);
		pa_stream = NULL;
	}

	if (pa_stream == NULL) {
		printf("Start stream.\n");
		pa_stream = pa_simple_new(NULL, "Songcast Receiver", PA_STREAM_PLAYBACK,
		                          NULL, "Songcast Receiver", &ss, NULL, &bufattr, NULL);
		current_ss = ss;
	}

	void *audio = frame->data + frame->codec_length;
	size_t audio_length = frame->channels * frame->bitdepth * htons(frame->samplecount) / 8;

	printf("frame flags %i frame %i audio_length %zi codec %.*s\n", frame->flags, ntohl(frame->frame), audio_length, frame->codec_length, frame->data);

	pa_simple_write(pa_stream, audio, audio_length, NULL);

	if (frame->flags & OHM1_FLAG_HALT) {
		printf("HALT received. Draining stream.\n");
		pa_simple_drain(pa_stream, NULL);
		pa_simple_free(pa_stream);
		pa_stream = NULL;
		return;
	}
}

void ohm_send_event(int fd, struct uri *uri, int event) {
	struct sockaddr_in dst = {
		.sin_family = AF_INET,
		.sin_port = htons(uri->port),
		.sin_addr.s_addr = inet_addr(uri->host)
	};

	ohm1_header message = {
		.signature = "Ohm ",
		.version = 1,
		.type = event,
		.length = htons(sizeof(ohm1_header))
	};

	if (sendto(fd, &message, sizeof(message), 0, (const struct sockaddr*) &dst, sizeof(dst)) < 0)
		error(1, errno, "Could not send message");
}

void dump_track(ohm1_track *track) {
	printf("Track URI: %.*s\n", htonl(track->uri_length), track->data);
	printf("Track Metadata: %.*s\n", htonl(track->metadata_length), track->data + htonl(track->uri_length));
}

void dump_metatext(ohm1_metatext *meta) {
	printf("Metatext: %.*s\n", htonl(meta->length), meta->data);
}

size_t slave_count;
struct sockaddr_in *my_slaves;

void update_slaves(ohm1_slave *slave) {
	free(my_slaves);
	slave_count = htonl(slave->count);
	printf("Updating slaves: %zi\n", slave_count);

	my_slaves = calloc(slave_count, sizeof(struct sockaddr_in));

	for (size_t i = 0; i < slave_count; i++) {
		my_slaves[i] = (struct sockaddr_in) {
			.sin_family = AF_INET,
			.sin_port = slave->slaves[i].port,
			.sin_addr.s_addr = slave->slaves[i].addr
		};
	}
}

void play_uri(struct uri *uri) {
	bool unicast = strncmp(uri->scheme, "ohu", 3) == 0;

	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (fd <= 0)
		error(1, errno, "Could not open socket");

	if (!unicast) {
		// Join multicast group
		struct sockaddr_in src = {
			.sin_family = AF_INET,
			.sin_port = htons(uri->port),
			.sin_addr.s_addr = inet_addr(uri->host)
		};

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
			error(1, 0, "setsockopt(SO_REUSEADDR) failed");

		if (bind(fd, (struct sockaddr *) &src, sizeof(src)) < 0)
			error(1, 0, "Could not bind socket");

		struct ip_mreq mreq = {
			mreq.imr_multiaddr.s_addr = inet_addr(uri->host)
		};

		if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
			error(1, 0, "Could not join multicast group");
	}

  uint8_t buf[4096];

	struct timeval timeout = {
		.tv_usec = 100000
	};

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
		error(1, errno, "setsockopt(SO_RCVTIMEO) failed");

	ohm_send_event(fd, uri, OHM1_JOIN);
	ohm_send_event(fd, uri, OHM1_LISTEN);

	struct timespec last_listen;
	clock_gettime(CLOCK_MONOTONIC, &last_listen);

	while (1) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);

		now.tv_nsec -= 750000000; // send listen every 750ms
		if (timespec_cmp(now, last_listen) > 0) {
			// TODO do not repeat join. Instead detect when we're not receving data anymore
			//ohm_send_event(fd, uri, OHM1_JOIN);
			ohm_send_event(fd, uri, OHM1_LISTEN);
			clock_gettime(CLOCK_MONOTONIC, &last_listen);
		}

		// TODO determine whether to send listen here
		ssize_t n = recv(fd, buf, sizeof(buf), 0);

		if (n < 0) {
			if (errno == EAGAIN)
				continue;

			error(1, errno, "recv");
		}

		if (n < sizeof(ohm1_header))
			continue;

		ohm1_header *hdr = (void *)buf;

		if (strncmp(hdr->signature, "Ohm ", 4) != 0)
			continue;

		if (hdr->version != 1)
			continue;

		// Forwarding
		if (slave_count > 0)
			switch (hdr->type) {
				case OHM1_AUDIO:
				case OHM1_TRACK:
				case OHM1_METATEXT:
					for (size_t i = 0; i < slave_count; i++) {
						// Ignore any errors when sending to slaves.
						// There is nothing we could do to help.
						sendto(fd, &buf, n, 0, (const struct sockaddr*) &my_slaves[i], sizeof(struct sockaddr));
					}
					break;
				default:
					break;
			}

		struct {
			ohm1_header hdr;
			uint32_t count;
			uint32_t frames[];
		} *lost = buf;

		switch (hdr->type) {
			case OHM1_LEAVE:
			case OHM1_JOIN:
			  // ignore join and leave
				break;
			case OHM1_LISTEN:
				if (!unicast)
					clock_gettime(CLOCK_MONOTONIC, &last_listen);
				break;
			case OHM1_AUDIO:
				play_frame((void*)buf);
				break;
			case OHM1_TRACK:
				dump_track((void *)buf);
				break;
			case OHM1_METATEXT:
				dump_metatext((void *)buf);
				break;
			case OHM1_SLAVE:
				update_slaves((void *)buf);
				break;
			case 7:
				for (int i = 0; i < ntohl(lost->count); i++) {
					printf("lost frame %i\n", ntohl(lost->frames[i]));
				}
			default:
				printf("Type %i not handled yet\n", hdr->type);
		}
	}
}

void open_uri(char *uri_string) {
	printf("Attemping to open %s\n", uri_string);

	struct uri *uri = parse_uri(uri_string);

	if (strcmp(uri->scheme, "ohz") == 0)
		resolve_ohz(uri);
	else if (strcmp(uri->scheme, "ohm") == 0 || strcmp(uri->scheme, "ohu") == 0)
		play_uri(uri);
	else
		error(1, 0, "unknown URI scheme \"%s\"", uri->scheme);

	free_uri(uri);
}

int main(int argc, char *argv[]) {
	LIBXML_TEST_VERSION

	int preset = 0;
	char *uri = NULL;

	int c;
	while ((c = getopt(argc, argv, "p:u:")) != -1)
		switch (c) {
			case 'p':
				preset = atoi(optarg);
				break;
			case 'u':
				uri = strdup(optarg);
				break;
		}

	if (uri != NULL && preset != 0)
		error(1, 0, "Can not specify both preset and URI!");


	if (preset != 0)
		uri = resolve_preset(preset);

	my_slaves = NULL;

	open_uri(uri);

	free(uri);
}
