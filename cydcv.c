/* glibc */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* external libs */
#include <curl/curl.h>
#include <yajl/yajl_parse.h>

/* macro */
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _cleanup_free_ _cleanup_(freep)
static inline void freep(void *p) { free(*(void**) p); }

#define API "YouDaoCV"
#define API_KEY "659600698"

#define YD_BASE_URL "http://fanyi.youdao.com"
#define YD_API_URL	YD_BASE_URL "/openapi.do?keyfrom=%s&key=%s&type=data&doctype=json&version=1.1&q=%s"

/* typedefs and objects */
enum json_key_type_t {
	JSON_KEY_METADATA,
	JSON_KEY_BASIC_DIC,
	JSON_KEY_WEB_DIC,
};
typedef enum json_key_type_t json_key_type_t;

struct key_t {
	const char *name;
	json_key_type_t type;
	int multivalued;
	size_t offset;
};

struct basic_dic_t {
	char *us_phonetic;
	char *phonetic;
	char *uk_phonetic;
	char **explains;
};
typedef struct basic_dic_t basic_dic_t;

struct web_dic_t {
	char **values;
	char *key;
};
typedef struct web_dic_t web_dic_t;

struct json_parser_t {
	const struct key_t *keys;

	char *translation;
	basic_dic_t *basic_dic;
	int depth;

	char *query;
	char *errorcode;
	web_dic_t *web_dic;
};
typedef struct json_parser_t json_parser_t;

/* function prototypes */
int json_string(void *ctx, const unsigned char *data, size_t size);
static int cyd_asprintf(char**, const char*, ...) __attribute__((format(printf,2,3)));

static yajl_callbacks callbacks = {
    NULL,			/* null */
    NULL,			/* boolean */
    NULL,			/* integer */
    NULL,			/* double */
    NULL,			/* number */
    json_string,	/* string */
    NULL,			/* start_map */
    NULL,			/* map_key */
    NULL,			/* end_map */
    NULL,			/* start_array */
    NULL,			/* end_array */
};

void *json_get_valueptr(json_parser_t *parser)
{
	unsigned char *addr = 0;
}

int json_string(void *ctx, const unsigned char *data, size_t size)
{
	json_parser_t *p = ctx;
	void *valueptr;

	valueptr = json_get_valueptr(p);
	if (valueptr == NULL)
		return 1;

	json_string_singlevalued(valueptr, data, size);
}

int json_string_singlevalued(char **dest, const unsigned char *data, size_t size)
{
	char *str;

	str = strndup((const char *)data, size);
	if (str == NULL)
		return 0;

	free(*dest);
	*dest = str;

	return 1;
}

int cyd_asprintf(char **string, const char *format, ...)
{
	int ret = 0;
	va_list args;

	va_start(args, format);
	ret = vasprintf(string, format, args);
	va_end(args);

	if (ret == -1) {
		fprintf(stderr, "failed to allocate string\n");
	}

	return ret;
}

size_t yajl_parse_stream(void *ptr, size_t size, size_t nmemb, void *stream)
{
	struct yajl_handle_t *hand = stream;
	size_t realsize = size * nmemb;

	yajl_parse(hand, ptr, realsize);

	return realsize;
}

int query(CURL *curl, const char *word)
{
	CURLcode curlstat;
	struct yajl_handle_t *yajl_hand = NULL;
	_cleanup_free_ char *url = NULL;
	long httpcode;
	json_parser_t json_parser;

	memset(&json_parser, 0, sizeof(json_parser_t));
	yajl_hand = yajl_alloc(&callbacks, NULL, &json_parser);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, yajl_parse_stream);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, yajl_hand);

	cyd_asprintf(&url, YD_API_URL, API, API_KEY, word);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	printf("curl_easy_perform %s\n", url);
	curlstat = curl_easy_perform(curl);

	if (curlstat != CURLE_OK) {
		fprintf(stderr, "%s\n", curl_easy_strerror(curlstat));
		return -1;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcode);
	printf("server responded with %ld\n", httpcode);
	if (httpcode >= 400) {
		fprintf(stderr, "error, server responded with HTTP %ld\n", httpcode);
		return -1;
	}

	yajl_complete_parse(yajl_hand);

	yajl_free(yajl_hand);

	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("usage: cydcv word\n");
		return -1;
	}

	const char *word = argv[1];
	printf("word to translate: %s\n", word);

	printf("initializing curl\n");
	CURL *curl = curl_easy_init();

	if (curl == NULL)
	{
		printf("failed to initialize curl\n");
		return -1;
	}

	query(curl, word);

	return 0;
}

