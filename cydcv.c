/* glibc */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
struct string_list_t {
	unsigned char *content;
	struct string_list_t *next;
};
typedef struct string_list_t string_list_t;

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
	string_list_t *explains;
};
typedef struct basic_dic_t basic_dic_t;

struct web_dic_t {
	string_list_t *value;
	char *key;
};
typedef struct web_dic_t web_dic_t;

struct web_dic_list_t {
	web_dic_t *web_dic;
	struct web_dic_list_t *next;
};
typedef struct web_dic_list_t web_dic_list_t;

struct json_parser_t {
	const struct key_t *key;

	string_list_t *translation;
	basic_dic_t *basic_dic;
	int depth;

	char *query;
	int errorcode;
	web_dic_t web_dic;
	web_dic_list_t *web_dic_list;
};
typedef struct json_parser_t json_parser_t;

/* function prototypes */
int json_end_map(void *ctx);
int json_integer(void *ctx, long long val);
int json_map_key(void *ctx, const unsigned char *data, size_t size);
int json_start_map(void *ctx);
int json_string(void *ctx, const unsigned char *data, size_t size);
int json_string_webdic_multivalued(web_dic_t *dest, const unsigned char *data, size_t size);
int json_string_multivalued(string_list_t **dest, const unsigned char *data, size_t size);
int json_string_singlevalued(char **dest, const unsigned char *data, size_t size);
const struct key_t *string_to_key(const unsigned char *key, size_t len);
static int cyd_asprintf(char**, const char*, ...) __attribute__((format(printf,2,3)));
void print_explanation(json_parser_t *parser);

static yajl_callbacks callbacks = {
    NULL,			/* null */
    NULL,			/* boolean */
    json_integer,	/* integer */
    NULL,			/* double */
    NULL,			/* number */
    json_string,	/* string */
    json_start_map,	/* start_map */
    json_map_key,	/* map_key */
    json_end_map,	/* end_map */
    NULL,			/* start_array */
    NULL,			/* end_array */
};

/* list must be sorted by the string value */
static const struct key_t json_keys[] = {
	{ "basic",			JSON_KEY_BASIC_DIC,	0, 0 },
	{ "errorcode",		JSON_KEY_METADATA,	0, offsetof(json_parser_t, errorcode) },
	{ "explains",		JSON_KEY_BASIC_DIC, 1, offsetof(basic_dic_t, explains) },
	{ "key",			JSON_KEY_WEB_DIC,	0, offsetof(web_dic_t, key) },
	{ "phonetic",		JSON_KEY_BASIC_DIC,	0, offsetof(basic_dic_t, phonetic) },
	{ "query",			JSON_KEY_METADATA,	0, offsetof(json_parser_t, query) },
	{ "translation",	JSON_KEY_METADATA,	1, offsetof(json_parser_t, translation) },
	{ "uk-phonetic",	JSON_KEY_BASIC_DIC,	0, offsetof(basic_dic_t, uk_phonetic) },
	{ "us-phonetic",	JSON_KEY_BASIC_DIC,	0, offsetof(basic_dic_t, us_phonetic) },
	{ "value",			JSON_KEY_WEB_DIC,	1, offsetof(web_dic_t, value) },
	{ "web",			JSON_KEY_WEB_DIC,	0, 0 },
};

string_list_t *string_list_add(string_list_t *list, unsigned char *str)
{
	printf("string_list_add: list - 0x%x, str - 0x%x\n",
			(unsigned int)list, (unsigned int)str);
	string_list_t *ptr, *lp;

	ptr = malloc(sizeof(string_list_t));
	if (ptr == NULL)
		return list;

	ptr->content = str;
	ptr->next = NULL;

	/* Special case: the input list is empty */
	if (list == NULL) {
		return ptr;
	}

	lp = list;
	while (lp->next)
		lp = lp->next;

	lp->next = ptr;
	return list;
}



void string_list_free(string_list_t *list)
{
	string_list_t *it = list;

	while (it) {
		string_list_t *tmp = it->next;
		free(it);
		it = tmp;
	}
}

void string_list_free_inner(string_list_t *list)
{
	if (list == NULL)
		return;

	string_list_t *it = list;

	while (it) {
		if (it->content)
			free(it->content);
		// memset(it, 0, sizeof(string_list_t));
		it = it->next;
	}
}

#define FREE_STRING_LIST(p) do { string_list_free_inner(p); string_list_free(p); p = NULL; } while(0)

void free_basic_dic(basic_dic_t *basic_dic)
{
	if (basic_dic == NULL)
		return;

	free(basic_dic->us_phonetic);
	free(basic_dic->phonetic);
	free(basic_dic->uk_phonetic);

	/*
	string_list_free(basic_dic->explains);
	string_list_t *it = basic_dic->explains;
	while (it) {
		string_list_t *tmp = it->next;
		free(it);
		it = tmp;
	}
	*/
	FREE_STRING_LIST(basic_dic->explains);

	memset(basic_dic, 0, sizeof(basic_dic_t));
}

void free_web_dic(web_dic_t *web_dic)
{
	if (web_dic == NULL)
		return;

	free(web_dic->key);
	/*
	string_list_free(web_dic->value);
	string_list_t *it = web_dic->value;
	while (it) {
		string_list_t *tmp = it->next;
		free(it);
		it = tmp;
	}
	*/
	FREE_STRING_LIST(web_dic->value);

	memset(web_dic, 0, sizeof(web_dic_t));
}

void free_web_dic_list(web_dic_list_t *list)
{
	web_dic_list_t *it = list;
	while (it) {
		web_dic_list_t *tmp = it->next;
		free(it);
		// memset(it, 0, sizeof(web_dic_list_t));
		it = tmp;
	}
}

void free_web_dic_list_inner(web_dic_list_t *list)
{
	web_dic_list_t *it = list;

	while (it) {
		if (it->web_dic) {
			free_web_dic(it->web_dic);
			free(it->web_dic);
		}
		it = it->next;
	}
}

#define FREE_WEB_DIC_LIST(p) do { free_web_dic_list_inner(p); free_web_dic_list(p); p = NULL; } while(0)

void json_parser_free_inner(json_parser_t *parser)
{
	if (parser == NULL)
		return;

	/* free allocated string fields */
	free(parser->query);

	/* free extended list info */
	FREE_STRING_LIST(parser->translation);
	free_basic_dic(parser->basic_dic);
	free(parser->basic_dic);
	FREE_WEB_DIC_LIST(parser->web_dic_list);
}

void json_parser_free(json_parser_t *parser)
{
	json_parser_free_inner(parser);
	free(parser);
}

web_dic_t *webdic_dup(web_dic_t *web_dic)
{
	web_dic_t *new_dic;

	new_dic = malloc(sizeof(web_dic_t));

	return new_dic ? memcpy(new_dic, web_dic, sizeof(web_dic_t)) : NULL;
}

web_dic_list_t *web_dic_list_add(web_dic_list_t *list, web_dic_t *dic)
{
	web_dic_list_t *ptr, *lp;

	ptr = malloc(sizeof(web_dic_list_t));
	if (ptr == NULL)
		return list;

	ptr->web_dic = dic;
	ptr->next = NULL;

	/* Special case: the input list is empty */
	if (list == NULL) {
		return ptr;
	}

	lp = list;
	while (lp->next)
		lp = lp->next;

	lp->next = ptr;
	return list;
}

void print_web_dic_list(web_dic_list_t *list)
{
	while (list) {
		web_dic_t *web = list->web_dic;
		printf("     * %s\n", web->key);
		string_list_t *curr = web->value;
		// print values in the same line
		printf("      ");
		while (curr) {
			printf(" %s;", curr->content);
			if (curr->next)
				curr = curr->next;
			else
				break;
		}
		printf("\n");

		if (list->next)
			list = list->next;
		else
			break;
	}
}

int json_end_map(void *ctx)
{
	json_parser_t *p = ctx;

	p->depth--;
	if (p->depth > 0) {
		if (p->key->type == JSON_KEY_WEB_DIC)
		{
			p->web_dic_list = web_dic_list_add(p->web_dic_list, webdic_dup(&p->web_dic));
			// print_web_dic_list(p->web_dic_list);
			// memset(p->web_dic, 0, sizeof(web_dic_t));
		}
		printf("json_end_map: type - %d, basic_dic - 0x%x, basic_dic_explains - 0x%x, web_dic_list - 0x%x\n",
				p->key->type, (unsigned int)p->basic_dic, (unsigned int)&p->basic_dic->explains,
				(unsigned int)&p->web_dic);
	}

	return 1;
}

void *json_get_valueptr(json_parser_t *parser)
{
	uint8_t *addr = 0;

	if (parser->key == NULL)
		return NULL;

	switch (parser->key->type) {
		case JSON_KEY_METADATA:
			addr = (uint8_t *)parser;
			break;
		case JSON_KEY_BASIC_DIC:
			addr = (uint8_t *)parser->basic_dic;
			break;
		case JSON_KEY_WEB_DIC:
			addr = (uint8_t *)&parser->web_dic;
			break;
	}
	printf("json_get_valueptr: type - %d, addr - 0x%x\n",
			parser->key->type, (unsigned int)addr);

	return addr + parser->key->offset;
}

int json_integer(void *ctx, long long val)
{
	json_parser_t *p = ctx;
	int *valueptr;

	valueptr = json_get_valueptr(p);
	if (valueptr == NULL)
		return 1;

	*valueptr = val;

	return 1;
}

int json_map_key(void *ctx, const unsigned char *data, size_t size)
{
	json_parser_t *p = ctx;

	p->key = string_to_key(data, size);

	return 1;
}

int json_start_map(void *ctx)
{
	json_parser_t *p = ctx;

	p->depth++;
	printf("json_start_map: depth - %d, json_parser_t - 0x%x\n",
			p->depth, (unsigned int)p);
	if (p->depth > 1) {
		if (p->key->type  == JSON_KEY_BASIC_DIC) {
			p->basic_dic = malloc(sizeof(basic_dic_t));
			memset(p->basic_dic, 0, sizeof(basic_dic_t));
		}
		else if (p->key->type == JSON_KEY_WEB_DIC) {
			// p->web_dic = malloc(sizeof(web_dic_t));
			memset(&p->web_dic, 0, sizeof(web_dic_t));
		}
		printf("json_start_map: type - %d, basic_dic - 0x%x, basic_dic_explains - 0x%x, web_dic_list - 0x%x\n",
				p->key->type, (unsigned int)p->basic_dic, (unsigned int)&p->basic_dic->explains,
				(unsigned int)&p->web_dic);
	}

	return 1;
}

int json_string(void *ctx, const unsigned char *data, size_t size)
{
	json_parser_t *p = ctx;
	void *valueptr;

	valueptr = json_get_valueptr(p);
	if (valueptr == NULL)
		return 1;

	printf("json_string: valueptr - 0x%x\n", (unsigned int)valueptr);
	if (p->key->multivalued)
		return json_string_multivalued(valueptr, data, size);
	else
		return json_string_singlevalued(valueptr, data, size);
}

int json_string_multivalued(string_list_t **dest, const unsigned char *data, size_t size)
{
	// printf("json_string_multivalued: dest - 0x%x, data - %s, size - %d\n",
	// 		dest, data, size);
	char *str;

	str = strndup((const char *)data, size);
	if (str == NULL)
		return 0;

	*dest = string_list_add(*dest, (unsigned char *)str);

	return 1;
}

int json_string_singlevalued(char **dest, const unsigned char *data, size_t size)
{
	// printf("json_string_singlevalued: dest - 0x%x, data - %s, size - %d\n",
	// 		dest, data, size);
	char *str;

	str = strndup((const char *)data, size);
	if (str == NULL)
		return 0;

	free(*dest);
	*dest = str;

	return 1;
}

int keycmp(const void *v1, const void *v2)
{
	const struct key_t *k1 = v1;
	const struct key_t *k2 = v2;

	return strcmp(k1->name, k2->name);
}

const struct key_t *string_to_key(const unsigned char *key, size_t len)
{
	char keybuf[32];
	struct key_t k;

	snprintf(keybuf, len + 1, "%s", key);

	k.name = keybuf;
	return bsearch(&k, json_keys, sizeof(json_keys) / sizeof(json_keys[0]),
			sizeof(json_keys[0]), keycmp);
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
	json_parser_t *json_parser;

	json_parser = malloc(sizeof(json_parser_t));
	memset(json_parser, 0, sizeof(json_parser_t));

	yajl_hand = yajl_alloc(&callbacks, NULL, json_parser);

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

	print_explanation(json_parser);

	yajl_free(yajl_hand);

	json_parser_free(json_parser);

	curl_easy_cleanup(curl);

	curl_global_cleanup();

	return 0;
}

void print_explanation(json_parser_t *parser)
{
	int has_result = 0;
	printf("%s", parser->query);
	if (parser->basic_dic != NULL) {
		has_result = 1;
		basic_dic_t *dic = parser->basic_dic;
		if (dic->uk_phonetic && dic->us_phonetic)
			printf(" UK: [%s], US: [%s]\n", dic->uk_phonetic, dic->us_phonetic);
		else if (dic->phonetic)
			printf(" [%s]\n", dic->phonetic);
		else
			printf("\n");

		if (dic->explains) {
			printf("  Word Explanation:\n");
			string_list_t *curr = dic->explains;
			while (curr->content) {
				printf("     * %s\n", curr->content);
				if (curr->next)
					curr = curr->next;
				else
					break;
			}
		} else
			printf("\n");
	} else if (parser->translation) {
		has_result = 1;
		printf("\n  Translation:\n");
		string_list_t *curr = parser->translation;
		while (curr->content) {
			printf("     * %s\n", curr->content);
			if (curr->next)
				curr = curr->next;
			else
				break;
		}
	} else
		printf("\n");

	if (parser->web_dic_list) {
		has_result = 1;
		printf("\n  Web Reference:\n");
		web_dic_list_t *list = parser->web_dic_list;
		while (list) {
			web_dic_t *web = list->web_dic;
			printf("     * %s\n", web->key);
			string_list_t *curr = web->value;
			// print values in the same line
			printf("      ");
			while (curr) {
				printf(" %s;", curr->content);
				if (curr->next)
					curr = curr->next;
				else
					break;
			}
			printf("\n");

			if (list->next)
				list = list->next;
			else
				break;
		}
	}

	if (has_result == 0)
		printf(" -- No result for this query.\n");

	printf("\n");
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
	curl_global_init(CURL_GLOBAL_ALL);
	CURL *curl = curl_easy_init();

	if (curl == NULL)
	{
		printf("failed to initialize curl\n");
		return -1;
	}

	query(curl, word);

	return 0;
}

