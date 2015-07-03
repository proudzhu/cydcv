/* glibc */
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* external libs */
#include <curl/curl.h>
#include <yajl/yajl_parse.h>
#include <readline/readline.h>
#include <readline/history.h>

/* macro */
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _cleanup_free_ _cleanup_(freep)
static inline void freep(void *p) { free(*(void**) p); }

// API KEY from ydcv
#define API "YouDaoCV"
#define API_KEY "659600698"

#define YD_BASE_URL "http://fanyi.youdao.com"
#define YD_API_URL	YD_BASE_URL "/openapi.do?keyfrom=%s&key=%s&type=data&doctype=json&version=1.1&q=%s"

#define NC                    "\033[0m"
#define BOLD                  "\033[1m"
#define UNDERLINE             "\033[4m"
#define BLINK                 "\033[5m"
#define REVERSE               "\033[7m"
#define CONCEALED             "\033[8m"

#define BLACK                 "\033[0;30m"
#define RED                   "\033[0;31m"
#define GREEN                 "\033[0;32m"
#define YELLOW                "\033[0;33m"
#define BLUE                  "\033[0;34m"
#define MAGENTA               "\033[0;35m"
#define CYAN                  "\033[0;36m"
#define WHITE                 "\033[0;37m"
#define BOLDBLACK             "\033[1;30m"
#define BOLDRED               "\033[1;31m"
#define BOLDGREEN             "\033[1;32m"
#define BOLDYELLOW            "\033[1;33m"
#define BOLDBLUE              "\033[1;34m"
#define BOLDMAGENTA           "\033[1;35m"
#define BOLDCYAN              "\033[1;36m"
#define BOLDWHITE             "\033[1;37m"

#define COLORFUL(x, COLOR)    COLOR x NC

/* typedefs and objects */
typedef enum __loglevel_t {
	LOG_INFO    = 1,
	LOG_ERROR   = (1 << 1),
	LOG_WARN    = (1 << 2),
	LOG_DEBUG   = (1 << 3),
	LOG_VERBOSE = (1 << 4),
	LOG_BRIEF   = (1 << 5)
} loglevel_t;

typedef enum __outlevel_t {
	OUT_DEFAULT = 1,
	OUT_SIMPLE	= (1 << 1),
	OUT_FULL	= (1 << 2),
} outlevel_t;

enum {
	OP_DEBUG = 1000,
};

struct list_t {
	void *data;
	struct list_t *next;
};
typedef struct list_t list_t;

typedef void (*list_fn_free)(void *); /* item deallocation callback */
typedef int (*list_fn_cmp)(const void *, const void *); /* item comparsion callback */

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
	list_t *explains;
};
typedef struct basic_dic_t basic_dic_t;

struct web_dic_t {
	list_t *value;
	char *key;
};
typedef struct web_dic_t web_dic_t;

struct json_parser_t {
	const struct key_t *key;

	list_t *translation;
	basic_dic_t *basic_dic;
	int depth;

	char *query;
	int errorcode;
	web_dic_t web_dic;
	list_t *web_dic_list;
};
typedef struct json_parser_t json_parser_t;

/* function prototypes */
int json_end_map(void *ctx);
int json_integer(void *ctx, long long val);
int json_map_key(void *ctx, const unsigned char *data, size_t size);
int json_start_map(void *ctx);
int json_string(void *ctx, const unsigned char *data, size_t size);
int json_string_webdic_multivalued(web_dic_t *dest, const unsigned char *data, size_t size);
int json_string_multivalued(list_t **dest, const unsigned char *data, size_t size);
int json_string_singlevalued(char **dest, const unsigned char *data, size_t size);
const struct key_t *string_to_key(const unsigned char *key, size_t len);
static int cyd_asprintf(char**, const char*, ...) __attribute__((format(printf,2,3)));
void print_explanation(json_parser_t *parser);

/* runtime configuration */
static struct {
    loglevel_t logmask;
	int out_full;
    int color;
	int selection;

	list_t *words;
} cfg;

/* globals */
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

int streq(const char *s1, const char *s2)
{
	return strcmp(s1, s2) == 0;
}

typedef const char * COLOR;

int cyd_vfprintf(FILE *stream, loglevel_t level, COLOR color, const char *format, va_list args)
{
    const char *prefix;
    char bufout[128];

    if(!(cfg.logmask & level)) {
        return 0;
    }

    switch(level) {
        case LOG_VERBOSE:
        case LOG_INFO:
            prefix = "";
            break;
        case LOG_ERROR:
            prefix = "ERROR: ";
            break;
        case LOG_WARN:
            prefix = "WARNNING: ";
            break;
        case LOG_DEBUG:
            prefix = "DEBUG: ";
            break;
        default:
            prefix = "";
            break;
    }

    if (cfg.color == 0)
	    color = NC;

    /* f.l.w.: 128 should be big enough... */
    snprintf(bufout, 128, "%s%s %s%s", color, prefix, format, NC);

    return vfprintf(stream, bufout, args);
}

int cyd_printf(loglevel_t level, COLOR color, const char *format, ...)
{
    int ret;
    va_list args;

    va_start(args, format);
    ret = cyd_vfprintf(stdout, level, color, format, args);
    va_end(args);

    return ret;
}

int cyd_fprintf(FILE *stream, loglevel_t level, const char *format, ...)
{
    int ret;
    va_list args;

    va_start(args, format);
    ret = cyd_vfprintf(stream, level, NC, format, args);
    va_end(args);

    return ret;
}


// linked list implemention from libalpm
list_t *list_add(list_t *list, void *data)
{
	list_t *ptr, *lp;

	ptr = malloc(sizeof(list_t));
	if (ptr == NULL)
		return list;

	ptr->data = data;
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

void list_free(list_t *list)
{
	list_t *it = list;

	while (it) {
		list_t *tmp = it->next;
		free(it);
		it = tmp;
	}
}

void list_free_inner(list_t *list, list_fn_free fn)
{
	list_t *it = list;

	if (fn) {
		while (it) {
			if (it->data)
				fn(it->data);
			it = it->next;
		}
	}
}

#define FREE_STRING_LIST(p) do { list_free_inner(p, free); list_free(p); p = NULL; } while(0)

void *list_find(const list_t *haystack, const void *needle, list_fn_cmp fn)
{
	const list_t *lp = haystack;
	while (lp) {
		if (lp->data && fn(lp->data, needle) == 0)
			return lp->data;
		lp = lp->next;
	}
	return NULL;
}

char *list_find_str(const list_t *haystack, const char *needle)
{
	return (char *)list_find(haystack, (const void *)needle,
			(list_fn_cmp)strcmp);
}

void free_basic_dic(basic_dic_t *basic_dic)
{
	if (basic_dic == NULL)
		return;

	free(basic_dic->us_phonetic);
	free(basic_dic->phonetic);
	free(basic_dic->uk_phonetic);

	FREE_STRING_LIST(basic_dic->explains);

	memset(basic_dic, 0, sizeof(basic_dic_t));
}

void free_web_dic_inner(void *web_dic)
{
	if (web_dic == NULL)
		return;

	web_dic_t *dic = (web_dic_t *)web_dic;

	free(dic->key);
	FREE_STRING_LIST(dic->value);

	memset(dic, 0, sizeof(web_dic_t));
}

void free_web_dic(void *web_dic)
{
	free_web_dic_inner(web_dic);
	free(web_dic);
}

#define FREE_WEB_DIC_LIST(p) do { list_free_inner(p, free_web_dic); list_free(p); p = NULL; } while(0)

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


int json_end_map(void *ctx)
{
	json_parser_t *p = ctx;

	p->depth--;
	if (p->depth > 0) {
		if (p->key->type == JSON_KEY_WEB_DIC)
		{
			p->web_dic_list = list_add(p->web_dic_list, webdic_dup(&p->web_dic));
		}
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
	cyd_printf(LOG_DEBUG, NC, "json_get_valueptr: type - %d, addr - 0x%x\n",
			(unsigned int *)parser->key->type, (unsigned int *)addr);

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
	cyd_printf(LOG_DEBUG, NC, "json_start_map: depth - %d, json_parser_t - 0x%x\n",
            p->depth, (unsigned int *)p);
	if (p->depth > 1) {
		if (p->key->type  == JSON_KEY_BASIC_DIC) {
			p->basic_dic = malloc(sizeof(basic_dic_t));
			memset(p->basic_dic, 0, sizeof(basic_dic_t));
		}
		else if (p->key->type == JSON_KEY_WEB_DIC) {
			// p->web_dic = malloc(sizeof(web_dic_t));
			memset(&p->web_dic, 0, sizeof(web_dic_t));
		}
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

	cyd_printf(LOG_DEBUG, NC, "json_string_multivalued: dest - 0x%x, data - %s, size - %d\n",
			valueptr, data, size);
	if (p->key->multivalued)
		return json_string_multivalued(valueptr, data, size);
	else
		return json_string_singlevalued(valueptr, data, size);
}

int json_string_multivalued(list_t **dest, const unsigned char *data, size_t size)
{
	char *str;

	str = strndup((const char *)data, size);
	if (str == NULL)
		return 0;

	*dest = list_add(*dest, (unsigned char *)str);

	return 1;
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
		cyd_fprintf(stderr, LOG_ERROR, "failed to allocate string\n");
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
	_cleanup_free_ char *escaped = NULL, *url = NULL;
	long httpcode;
	json_parser_t *json_parser;

	json_parser = malloc(sizeof(json_parser_t));
	memset(json_parser, 0, sizeof(json_parser_t));

	yajl_hand = yajl_alloc(&callbacks, NULL, json_parser);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, yajl_parse_stream);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, yajl_hand);

	escaped = curl_easy_escape(curl, word, strlen(word));
	if (escaped) {
		cyd_printf(LOG_DEBUG, NC, "Encoded: %s\n", escaped);
	}

	cyd_asprintf(&url, YD_API_URL, API, API_KEY, escaped);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	cyd_printf(LOG_DEBUG, NC, "curl_easy_perform %s\n", url);
	curlstat = curl_easy_perform(curl);

	if (curlstat != CURLE_OK) {
		cyd_fprintf(stderr, LOG_ERROR, "%s\n", curl_easy_strerror(curlstat));
		return -1;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcode);
	cyd_printf(LOG_DEBUG, NC, "server responded with %ld\n", httpcode);
	if (httpcode >= 400) {
		cyd_fprintf(stderr, LOG_ERROR, "error, server responded with HTTP %ld\n", httpcode);
		return -1;
	}

	yajl_complete_parse(yajl_hand);

	print_explanation(json_parser);

	yajl_free(yajl_hand);

	json_parser_free(json_parser);

	return 0;
}

void print_explanation(json_parser_t *parser)
{
	int has_result = 0;
	cyd_printf(LOG_INFO, UNDERLINE, "%s", parser->query);
	if (parser->basic_dic != NULL) {
		has_result = 1;
		basic_dic_t *dic = parser->basic_dic;
		if (dic->uk_phonetic && dic->us_phonetic) {
			// cyd_printf(LOG_INFO, NC, " UK: [%s], US: [%s]\n", dic->uk_phonetic, dic->us_phonetic);
			cyd_printf(LOG_INFO, NC, " UK: [");
			cyd_printf(LOG_INFO, YELLOW, "%s", dic->uk_phonetic);
			cyd_printf(LOG_INFO, NC, "], US: [");
			cyd_printf(LOG_INFO, YELLOW, "%s", dic->us_phonetic);
			cyd_printf(LOG_INFO, NC, "]\n");
		}
		else if (dic->phonetic) {
			// cyd_printf(LOG_INFO, NC, " [%s]\n", dic->phonetic);
			cyd_printf(LOG_INFO, NC, " [");
			cyd_printf(LOG_INFO, YELLOW, "%s", dic->phonetic);
			cyd_printf(LOG_INFO, NC, "]\n");
		}
		else
			cyd_printf(LOG_INFO, NC, "\n");

		if (dic->explains) {
			cyd_printf(LOG_INFO, CYAN, "  Word Explanation:\n");
			list_t *curr = dic->explains;
			while (curr->data) {
				cyd_printf(LOG_INFO, NC, "     * %s\n", (unsigned char *)curr->data);
				if (curr->next)
					curr = curr->next;
				else
					break;
			}
		} else
			cyd_printf(LOG_INFO, NC, "\n");
	} else if (parser->translation) {
		has_result = 1;
		cyd_printf(LOG_INFO, CYAN, "\n  Translation:\n");
		list_t *curr = parser->translation;
		while (curr->data) {
			cyd_printf(LOG_INFO, NC, "     * %s\n", (unsigned char *)curr->data);
			if (curr->next)
				curr = curr->next;
			else
				break;
		}
	} else
		cyd_printf(LOG_INFO, NC, "\n");

	if (cfg.out_full && parser->web_dic_list) {
		has_result = 1;
		cyd_printf(LOG_INFO, CYAN, "\n   Web Reference:\n");
		list_t *list = parser->web_dic_list;
		while (list) {
			web_dic_t *web = list->data;
			// cyd_printf(LOG_INFO, NC, "     * %s\n", web->key);
			cyd_printf(LOG_INFO, NC, "     * ");
			cyd_printf(LOG_INFO, YELLOW, "%s\n", web->key);
			list_t *curr = web->value;
			// print values in the same line
			cyd_printf(LOG_INFO, NC, "      ");
			while (curr) {
				// cyd_printf(LOG_INFO, NC, " %s;", (unsigned char *)curr->data);
				cyd_printf(LOG_INFO, MAGENTA, " %s", curr->data);
				if (curr->next) {
					cyd_printf(LOG_INFO, NC, ";");
					curr = curr->next;
				}
				else
					break;
			}
			cyd_printf(LOG_INFO, NC, "\n");

			if (list->next)
				list = list->next;
			else
				break;
		}
	}

	if (has_result == 0)
		cyd_printf(LOG_INFO, NC, " -- No result for this query.\n");

	cyd_printf(LOG_INFO, NC, "\n");
}

void usage(void)
{
	fprintf(stderr, "usage: cydcv [-h] [-f] [-s] [-x] [--color {always,auto,never}]\n");
	fprintf(stderr, "             [words [words ...]]\n\n");
	fprintf(stderr, "Youdao Console Version\n\n");
	fprintf(stderr,
			"positional arguments:\n"
			"  words                 words to lookup, or quoted sentences to translate.\n\n");
	fprintf(stderr,
			"optional arguments:\n"
			"  -h, --help            show this help message and exit\n"
			"  -f, --full            print full web reference, only the first 3 results\n"
			"                        will be printed without this flag.\n"
			"  -s, --simple          only show explainations. argument \"-f\" will not take\n"
			"                        effect\n"
			"  -x, --selection       show explaination of current selection.\n"
			"  -c, --color {always,auto,never}\n"
			"                        colorize the output. Default to 'auto' or can be\n"
			"                        'never' or 'always'.\n"
			"  --debug               show debug info\n\n");
}

int parse_options(int argc, char **argv)
{
	int opt, option_index = 0;

	static const struct option opts[] = {
		/* options */
		{"full",		no_argument,		0, 'f'},
		{"simple",		no_argument,		0, 's'},
		{"selection",	no_argument,		0, 'x'},
		{"color",		optional_argument,	0, 'c'},
		{"debug",		no_argument,		0, OP_DEBUG},
		{"help",		no_argument,		0, 'h'},
		{0,				0,					0, 0},
	};

	while((opt = getopt_long(argc, argv, "fsxch", opts, &option_index)) != -1) {
		cyd_printf(LOG_DEBUG, NC, "parse_options: opt - 0x%x\n", opt);
		switch (opt) {
			/* options */
			case 'f':
				cfg.out_full = 1;
				break;
			case 's':
				cfg.out_full = 0;
				break;
			case 'x':
				cfg.selection = 1;
				break;
			case 'c':
				if(!optarg || streq(optarg, "auto")) {
					if(isatty(fileno(stdout))) {
						cfg.color = 1;
					} else {
						cfg.color = 0;
					}
				} else if(streq(optarg, "always")) {
					cfg.color = 1;
				} else if(streq(optarg, "never")) {
					cfg.color = 0;
				} else {
					fprintf(stderr, "invalid argument to --color\n");
					return 1;
				}
				break;
			case OP_DEBUG:
				cfg.logmask |= LOG_DEBUG;
				break;
			case 'h':
			default:
				usage();
				exit(0);
		}
	}

	while (optind < argc) {
		if (!list_find_str(cfg.words, argv[optind])) {
			cyd_printf(LOG_DEBUG, NC, "add words: %s\n", argv[optind]);
			cfg.words = list_add(cfg.words, strdup(argv[optind]));
		}
		optind++;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ret;

	/* initialize config */
	cfg.logmask = LOG_INFO|LOG_ERROR;
	cfg.out_full = 1;
	cfg.color = 0;
	cfg.selection = 0;

	if (isatty(fileno(stdout)))
		cfg.color = 1;

	ret = parse_options(argc, argv);
	if (ret)
	{
		usage();
		return ret;
	}

	cyd_printf(LOG_DEBUG, NC, "initializing curl\n");
	curl_global_init(CURL_GLOBAL_ALL);
	CURL *curl = curl_easy_init();

	if (curl == NULL)
	{
		cyd_fprintf(stderr, LOG_ERROR, "failed to initialize curl\n");
		return -1;
	}

 	list_t *word = cfg.words;
	while (word) {
		cyd_printf(LOG_DEBUG, NC, "word to translate: %s\n", word->data);

		query(curl, word->data);

		if (word->next)
			word = word->next;
		else
			break;
	}

	if (word == NULL) {
		if (cfg.selection) {
			FILE *file;
			char last[128], curr[128];

			file = popen("xsel", "r");
			cyd_printf(LOG_INFO, NC, "Waiting for selection>\n");
			fgets(last, 128, file);
			fclose(file);
			while (1) {
				sleep(1);
				file = popen("xsel", "r");
				fgets(curr, 128, file);
				fclose(file);
				if (streq(last, curr) == 0) {
					memcpy(last, curr, 128);
					query(curl, last);
					cyd_printf(LOG_INFO, NC, "Waiting for selection>\n");
				}
			}
			goto done;
		}
		while (1) {
			char *line = readline("> ");
			add_history(line);
			if (line == NULL) {
				printf("\nBye\n");
				break;
			} else {
				query(curl, line);
				free(line);
			}
		}
	}

done:
	curl_easy_cleanup(curl);

	curl_global_cleanup();

	return 0;
}

