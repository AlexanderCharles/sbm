
/******************************************************************************
 * 
 * portfolio/BugTracker
 * Copyright (C) 2023 github.com/AlexanderCharles
 * 
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published
 * by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * version 3 for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 *****************************************************************************/

/******************************************************************************
 * 
 * What is this program?
 *	Simple Bookmark Manager is a CLI substitute for web-browser bookmarks.
 * 
 * What can this program do?
 *	Bookmarks can be added/removed/updated, queried, and opened.
 * 
 * What is the goal of this program?
 * 	To create a Suckless-style, simple alternative to Buku, which is easily
 * 	modifiable.
 * 
 * How do I use this program?
 * 	sbm add <link> [OPTIONS]
 * 		These options must be followed by a value.
 * 		-c <comment>               to add a comment.
 * 		-t <title>                 to add a custom title.
 * 		-tg <tag-id> OR <tag-name> to add tag(s).
 * 	sbm update <ID> [at least one option]
 * 		-c <comment>               to update a comment.
 * 		-t <title>                 to update a custom title.
 * 		-tg <tag-ID> OR <tag-name> to add/remove tag(s).
 * 	sbm remove <ID>
 * 	sbm open   <ID>
 * 	sbm list <term> [OPTIONS]
 * 		<term> pertains the title. "all" can be used to list every entry.
 * 		-tg <tag-ID> OR <tag-name> to list entries with a specified tag.
 * 
 * Tags behave similarly.
 * 	sbm tag add <term>
 * 		The first letter cannot be a number.
 * 	sbm tag rename <tag-ID> OR <tag-name> <term>
 * 		Parameter 1 is the ID of the tag which is being changed. Parameter 2 is
 * 		the value the tag is changed to.
 * 		The new name must not begin with a number.
 * 	sbm tag remove <tag-ID> OR <tag-name>
 * 	sbm tag list all
 *
 * How do I compile and install this program?
 * 	Firstly, the dependencies must be installed: libcurl and json.h.
 * 		(At least on Debian) libcurl4 comes with different "flavours": NSS, 
 * 		OpenSSL, and GnuTLS). I believe any of these will be fine.
 * 		json.h can be downloaded from here: https://github.com/sheredom/json.h
 * 	Build with 'make', install with 'make install'.
 * 
 * What problems does this software have?
 * 	1. Memory is not freed before calling exit(...) in most cases.
 * 	2. Multiple downloads (read GetWebpage()'s "NOTE:" comment).
 * 
 *****************************************************************************/



#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>
#ifdef __TINYC__
#define __GNUC__
#endif
#include <json.h>
#ifdef __TINYC__
#undef __GNUC__
#endif

#include "config.h"



#define Min(A, B) (((A) < (B)) ? (A) : (B))
#define Max(A, B) (((A) > (B)) ? (A) : (B))

#define true  1
#define false 0



typedef struct URL {
	union {
		char  s[S_ADDR_S];
		char* l;
	} address;
	int long_url;
} URL;

typedef struct Table {
	struct Row {
		unsigned int id;
		URL          url;
		
		char         title  [TITLE_S];
		unsigned int tag_ids[ROW_TAG_C];
		char         comment[COMMENT_S];
		
		struct DateTime {
			int d_y, d_m, d_d;
			int t_h, t_m, t_s;
			
			char last_updated[20];
		} datetime;
	} *rows;

	unsigned int count;
	unsigned int next_UID;
} Table;

typedef struct DateTime DateTime;
typedef struct Row Row;
typedef struct Tag Tag;

typedef struct Tags {
	struct Tag {
		unsigned int id;
		char         name[TAG_NAME_S];
	} *tags;
	
	unsigned int count;
	unsigned int next_UID;
} Tags;

typedef struct Core {
	Table table;
	Tags  tags;
} Core;

typedef struct InputArgs {
	enum InputMode {
		IM_INVALID,
		
		IM_ADD,
		IM_UPDATE,
		IM_REMOVE,
		IM_OPEN,
		
		IM_LIST,
		IM_TAG_LIST,
		
		IM_TAG_ADD,
		IM_TAG_ADD_TO_ENTRY,
		IM_TAG_RENAME,
		IM_TAG_REMOVE
	} input_mode;
	
	enum WordIndices {
		WI_MOD = 0,
		
		WI_TITLE   = 1,
		WI_COMMENT = 2,
		WI_TAG     = 3,
		
		WI_COUNT
	} WordIndices;
	char* word_buffers[WI_COUNT];
} InputArgs;

typedef struct CURLData {
	char* contents;
	int   index;
	int   contents_length;
} CURLData;



static int WriteJSON(Core* c);

static char* stristr(const char* a, const char* b);
static int   stricmp(const char* a, const char* b);
static char* strcpyt(char* d, char* s, unsigned int m, int l);

static void PrintWithTagIDs(unsigned int* tagIds, unsigned int tc,
                           Row* r, unsigned int rc);
static void PrintRow(Row r, Tags tg);

static char*        GetTagName(Tags t, unsigned int id);
static unsigned int GetTagID  (Tags t, char* s);

static void GetCurrentDateTime(DateTime* o_dt);

static void UpdateRowTags(Core* io_c, unsigned int rowIndex, InputArgs* ia);

static void      GetPageTitle(char* contents, char* o_buffer);
static CURLData* GetWebpage(char* url);

static void         ValidateTagName(char* io_buffer[],
                                    const unsigned int index);
static unsigned int GetInputTagIndex(Core* c, char* buffers[],
                                     const unsigned int inputIndex);

static void GetConfigPath(char* o_buffer);


static InputArgs
ParseEntryInput(char* args[], int argc)
{
	InputArgs result;
	int i;

	memset(&result, 0, sizeof(InputArgs));
	if (strcmp(args[0], "add") == 0) {
		assert(argc > 1 && argc <= 8);
		if (argc < 2) {
			printf("Attempting to add a new URL but no URL provided.\n");
			exit(-1);
			printf("Too many args. Perhaps you did not enclose a list with " \
			       "\".\n");
			exit(-1);
		}
		result.input_mode = IM_ADD;
		result.word_buffers[WI_MOD] = args[1];
		
		for (i = 0; i < argc; ++i) {
			if ((args[i][0] == '-') && (i + 1 > argc)) {
				printf("Has option flag but no option given\n");
				exit(-1);
			}
			
			if (strcmp(args[i], "-c") == 0) {
				result.word_buffers[WI_COMMENT] = args[i + 1];
				result.input_mode = IM_ADD;
			} else if (strcmp(args[i], "-t") == 0) {
				result.word_buffers[WI_TITLE] = args[i + 1];
				result.input_mode = IM_ADD;
			} else if (strcmp(args[i], "-tg") == 0) {
				result.word_buffers[WI_TAG] = args[i + 1];
				result.input_mode = IM_ADD;
			}
		}
	} else if (strcmp(args[0], "update") == 0) {
		for (i = 0; i < argc; ++i) {
			if ((args[i][0] == '-') && (i + 1 > argc)) {
				printf("Has option flag but no option given\n");
				exit(-1);
			}
			
			result.input_mode = IM_UPDATE;
			result.word_buffers[WI_MOD] = args[1];
			if (strcmp(args[i], "-c") == 0) {
				result.word_buffers[WI_COMMENT] = args[i + 1];
			} else if (strcmp(args[i], "-t") == 0) {
				result.word_buffers[WI_TITLE] = args[i + 1];
			} else if (strcmp(args[i], "-tg") == 0) {
				result.word_buffers[WI_TAG] = args[i + 1];
			}
		}
	} else if (strcmp(args[0], "remove") == 0) {
		if (argc < 2) {
			printf("Attempting to remove entry but no row ID provided\n");
			exit(-1);
		}
		result.input_mode = IM_REMOVE;
		result.word_buffers[WI_MOD] = args[1];
	} else if (strcmp(args[0], "list") == 0) {
		result.input_mode = IM_LIST;
		if (argc == 2) {
			result.word_buffers[WI_MOD] = args[1];
		} else if (argc == 3) {
			if (stricmp(args[1], "-tg") == 0) {
				result.word_buffers[WI_TAG] = args[2];
			} else {
				printf("Invalid input");
				exit(-1);
			}
		} else {
			printf("Invalid input\n");
			exit(-1);
		}
	} else if (strcmp(args[0], "open") == 0) {
		result.input_mode = IM_OPEN;
		result.word_buffers[WI_MOD] = args[1];
	}
	
	return result;
}

static InputArgs
ParseTagInput(char* args[], int argc)
{
	InputArgs result;
	
	memset(&result, 0, sizeof(result));
	args = &args[1];
	argc--;
	
	if (argc < 2) {
		printf("Too few args for interacting with tags.\n");
		exit(-1);
	} else if (argc > 3) {
		printf("Too many args for interacting with tags. Perhaps you forgot" \
		       " to surround values with \"\".\n");
		exit(-1);
	}
	
	if (strcmp(args[0], "add") == 0) {
		result.input_mode = IM_TAG_ADD;
		result.word_buffers[WI_MOD] = args[1];
	} else if (strcmp(args[0], "rename") == 0) {
		result.input_mode = IM_TAG_RENAME;
		result.word_buffers[WI_MOD] = args[1];
		result.word_buffers[WI_TAG] = args[2];
	} else if (strcmp(args[0], "remove") == 0) {
		result.input_mode = IM_TAG_REMOVE;
		result.word_buffers[WI_MOD] = args[1];
	} else if (strcmp(args[0], "list") == 0) {
		result.input_mode = IM_TAG_LIST;
		result.word_buffers[WI_MOD] = args[1];
	} else {
		result.input_mode = IM_TAG_ADD_TO_ENTRY;
		result.word_buffers[WI_MOD] = args[0];
		result.word_buffers[WI_TAG] = args[1];
	}
	
	return result;
}

static Core
ReadJSON(void)
{
	Core result;
	
	char* contents = NULL;
	size_t contentsSize = 0;
	struct json_value_s* root;
	struct json_object_element_s* tagsHandle, *rowsHandle;
	Tags tags   = { 0 };
	Table table = { 0 };
	int rowUID = 0;
	
	{
		FILE* jsonFile = NULL;
		char filename[512] = { 0 };
		DIR* dir;
		
		GetConfigPath(filename);
		if ((dir = opendir(filename))) {
			closedir(dir);
		} else {
			if (mkdir(filename, 0777) < 0) {
				fprintf(stderr, "Could not create directory '%s'.\n", filename);
				exit(-1);
			}
		}
		strcat(filename, cache_filename);
		
		jsonFile = fopen(filename, "rb");
		
		if (!jsonFile) {
			char confirmation;
			printf("Could not find '%s'.\nWould you like to create a new config " \
			       "file? [Y/n] ", filename);
			scanf("%c", &confirmation);
			if (!(confirmation == 'y' || confirmation == 'Y')) {
				exit(0);
			}
			
			memset(&result, 0, sizeof(Core));
			result.table.next_UID = 1;
			result.table.rows = malloc(sizeof(Row));
			memset(result.table.rows, 0, sizeof(Row));
			result.tags.next_UID = 1;
			result.tags.tags = malloc(sizeof(Tag));
			memset(result.tags.tags, 0, sizeof(Tag));
			
			WriteJSON(&result);
			printf("Config created. You may need to reperform your last command.\n");
			exit(0);
		}
		
		{
			fseek(jsonFile, 0, SEEK_END);
			contentsSize = ftell(jsonFile);
			fseek(jsonFile, 0, SEEK_SET);
			
			contents = malloc(sizeof(char) * contentsSize + 1);
			memset(contents, 0, sizeof(char) * contentsSize + 1);
			
			fread(contents, contentsSize, 1, jsonFile);
		}
		fclose(jsonFile);
	}
	
	{
		struct json_object_s* obj;
		
		root = json_parse(contents, contentsSize);
		free(contents);
		assert(root);
		assert(root->type == json_type_object);
		
		obj = (struct json_object_s*) root->payload;
		assert(obj->length == 2);
		
		tagsHandle = obj->start;
	}
	
	assert(strcmp(tagsHandle->name->string, "tags") == 0);
	
	{
		int i = 0;
		struct json_object_element_s* item;
		struct json_object_s* entry;
		
		entry = (struct json_object_s*) tagsHandle->value->payload;
		assert(entry);
		
		item = entry->start;
		/* Allocate one extra. If the user uses the 'tag add' command, it will
		   save time by not having to realloc. */
		tags.tags = malloc(sizeof(Tag) * ((tags.count = entry->length) + 1));
		memset(tags.tags, 0, sizeof(Tag) * tags.count + 1);
		
		while (item != NULL) {
			struct json_string_s* value;
			int len;
			
			value = item->value->payload;
			tags.tags[i].id = atoi(item->name->string);
			tags.next_UID = Max(tags.tags[i].id, tags.next_UID);
			len = Min(strlen(value->string), TAG_NAME_S - 1);
			strncpy(tags.tags[i].name, value->string, len);
			
			i++;
			item = item->next;
		}
	}
	
	rowsHandle = tagsHandle->next;
	assert(strcmp(rowsHandle->name->string, "rows") == 0);
	
	{
		int i = 0;
		struct json_object_s* entry;
		struct json_object_element_s* item;
		
		entry = rowsHandle->value->payload;
		assert(entry);
		
		item = entry->start;
		/* Allocate one extra. If the user uses the 'add' command, it will
		   save time by not having to realloc. */
		table.rows = malloc(sizeof(Row) * ((table.count = entry->length) + 1));
		memset(table.rows, 0, sizeof(Row) * table.count + 1);
		
		while (item != NULL) {
			Row row;
			int len; 
			int j;
			struct json_string_s* value;
			struct json_array_element_s* arrayItem;
			
			memset(&row, 0, sizeof(Row));
			row.id = atoi(item->name->string);
			rowUID = Max(row.id, rowUID);
			
			{
				struct json_value_s* itemValue;
				struct json_array_s* arrayEntry;
				
				itemValue = item->value;
				assert(itemValue);
				arrayEntry = itemValue->payload;
				assert(arrayEntry);
				
				arrayItem = arrayEntry->start;
				assert(arrayItem);
			}
			{
				assert(arrayItem->value->type == json_type_string);
				assert((value = json_value_as_string(arrayItem->value)));
				
				len = strlen(value->string);
				if (len > S_ADDR_S - 1) {
					row.url.long_url = true;
					row.url.address.l = malloc(sizeof(char) * (len + 1));
					strncpy(row.url.address.l, value->string, len);
					row.url.address.l[len] = '\0';
				} else {
					strncpy(row.url.address.s, value->string,
					        Min(len, S_ADDR_S - 1));
					row.url.address.s[len] = '\0';
				}
				
				arrayItem = arrayItem->next;
				assert(arrayItem);
			}
			{
				assert(arrayItem->value->type == json_type_string);
				assert((value = json_value_as_string(arrayItem->value)));
				
				len = Min(TITLE_S - 1, strlen(value->string));
				strncpy(row.title, value->string, Min(len, TITLE_S - 1));
				
				arrayItem = arrayItem->next;
			}
			{
				assert(arrayItem->value->type == json_type_string);
				assert((value = json_value_as_string(arrayItem->value)));
				
				len = Min(COMMENT_S - 1, strlen(value->string));
				strncpy(row.comment, value->string, Min(len, COMMENT_S - 1));
				
				arrayItem = arrayItem->next;
			}
			{
				assert(arrayItem->value->type == json_type_string);
				assert((value = json_value_as_string(arrayItem->value)));
				
				assert(strlen(value->string) < 20);
				len = Min(19, strlen(value->string));
				strncpy(row.datetime.last_updated, value->string, Min(len, 19));
				if (sscanf(row.datetime.last_updated,
				           "%d-%d-%d %d:%d:%d",
				           &row.datetime.d_y, &row.datetime.d_m, &row.datetime.d_d,
				           &row.datetime.t_h, &row.datetime.t_m, &row.datetime.t_s)
				     < 1) {
					printf("Could not parse date and time\n");
				}
				
				arrayItem = arrayItem->next;
			}
			{
				struct json_array_s* tagArray;
				struct json_array_element_s* tagItem;
				
				assert(arrayItem->value->type == json_type_array);
				assert((tagArray = json_value_as_array(arrayItem->value)));
				assert((tagItem = tagArray->start));
				j = 0;
				
				while (tagItem != NULL) {
					assert((value = json_value_as_string(tagItem->value)));
					row.tag_ids[j] = atoi(value->string);
					tagItem = tagItem->next;
					j++;
				}
			}
			
			table.rows[i] = row;
			i++;
			item = item->next;
		}
		
	}
	
	free(root);
	
	result.table = table;
	result.tags = tags;
	result.tags.next_UID += 1;
	result.table.next_UID = rowUID + 1;
	
	return result;
}

static int
WriteJSON(Core* c)
{
	char LARGE_BUFFER[10000];
	int i, j;
	
	memset(LARGE_BUFFER, 0, sizeof(LARGE_BUFFER));
	
	strcpy(LARGE_BUFFER, "{\n\t\"tags\":{\n");
	for (i = 0; i < c->tags.count; ++i) {
		if (c->tags.tags[i].id == 0) continue;
		sprintf(&LARGE_BUFFER[strlen(LARGE_BUFFER)],
				"\t\t\"%d\": \"%s\"",
				c->tags.tags[i].id, c->tags.tags[i].name);
		if (i != c->tags.count - 1) {
			strcat(&LARGE_BUFFER[strlen(LARGE_BUFFER)], ",\n");
		} else {
			strcat(&LARGE_BUFFER[strlen(LARGE_BUFFER)], "\n");
		}
	}
	strcpy(&LARGE_BUFFER[strlen(LARGE_BUFFER)], "\t},\n\t\"rows\":{\n");
	for (i = 0; i < c->table.count; ++i) {
		Row* curr;
		char* url;
		
		curr = &c->table.rows[i];
		if (curr->id == 0) continue;
		
		if (curr->url.long_url) {
			url = curr->url.address.l;
		} else {
			url = curr->url.address.s;
		}
		
		sprintf(&LARGE_BUFFER[strlen(LARGE_BUFFER)],
		        "\t\t\"%d\": [\"%s\", \"%s\", \"%s\", \"%s\", [",
		        curr->id, url, curr->title, curr->comment,
		        curr->datetime.last_updated);
		for (j = 0; j < ROW_TAG_C; ++j) {
			sprintf(&LARGE_BUFFER[strlen(LARGE_BUFFER)], "\"%d\"", curr->tag_ids[j]);
			if (j != ROW_TAG_C - 1) {
				strcat(&LARGE_BUFFER[strlen(LARGE_BUFFER)], ", ");
			}
		}
		
		if (i != c->table.count - 1) {
			strcat(&LARGE_BUFFER[strlen(LARGE_BUFFER)], "]],\n");
		} else {
			strcat(&LARGE_BUFFER[strlen(LARGE_BUFFER)], "]]\n");
		}
	}
	strcat(&LARGE_BUFFER[strlen(LARGE_BUFFER)], "\t}\n}\n");
	
	{
		FILE* fp;
		char filename[512] = { 0 };
		
		GetConfigPath(filename);
		strcat(filename, cache_filename);
		fp = fopen(filename, "w");
		if (fp == NULL) {
			return -1;
		}
		if ((fwrite(LARGE_BUFFER, strlen(LARGE_BUFFER), 1, fp) < 1)) {
			fclose(fp);
			return -1;
		}
		fclose(fp);
	}
	
	return 1;
}

static void
ProcessCommand(Core* io_c, InputArgs* ia)
{
	switch (ia->input_mode) {
		case IM_INVALID:
			{
				printf("Invalid input\n");
				exit(-1);
			}
			break;
		case IM_ADD:
			{
				Row* row;
				
				row = &io_c->table.rows[io_c->table.count];
				row->id = io_c->table.next_UID++;
				strcpy(row->url.address.s, ia->word_buffers[WI_MOD]);
				if (ia->word_buffers[WI_TITLE] != NULL) {
					strcpyt(row->title, ia->word_buffers[WI_TITLE], TITLE_S,
					        -1);
				} else {
					CURLData* data;
					
					data = GetWebpage(ia->word_buffers[WI_MOD]);
					GetPageTitle(data->contents, row->title);
					
					free(data->contents);
					free(data);
				}
				
				if (ia->word_buffers[WI_COMMENT] != NULL) {
					strcpyt(row->comment, ia->word_buffers[WI_COMMENT],
					        COMMENT_S, -1);
				}
				
				if (ia->word_buffers[WI_TAG] != NULL) {
					char* multipleWords;
					
					multipleWords = strstr(ia->word_buffers[WI_TAG], " ");
					if (multipleWords == NULL) {
						if (isdigit(ia->word_buffers[WI_TAG][0])/* == true*/) { 
								row->tag_ids[0] = atoi(ia->word_buffers[WI_TAG]);
							} else {
								int id;
								
								if ((id =
								     GetTagID(io_c->tags,
								              ia->word_buffers[WI_TAG])) == 0) {
									fprintf(stderr, "Invalid tag name '%s'\n",
									        ia->word_buffers[WI_TAG]);
								} else {
									row->tag_ids[0] = id;
								}
							}
					} else {
						char* curr;
						int i;
						
						i = 0;
						curr = strtok(ia->word_buffers[WI_TAG], " ");
						while (curr != NULL) {
							if (isdigit(curr[0])/* == true*/) { 
								row->tag_ids[i++] = atoi(curr);
							} else {
								int id;
								
								if ((id = GetTagID(io_c->tags, curr)) == 0) {
									fprintf(stderr,
									        "Invalid tag name - %s (id = %d)\n",
									        curr, id);
								} else {
									row->tag_ids[i++] = id;
								}
							}
							curr = strtok(0, " ");
						}
					}
				}
				GetCurrentDateTime(&row->datetime);
				
				io_c->table.count += 1;
			}
			break;
		case IM_UPDATE:
			{
				unsigned int i;
				int id, index;
				
				if (!isdigit(ia->word_buffers[WI_MOD][0])) {
					printf("Arg 1 must be the URL ID which you want to " \
				           "update\n");
					exit(-1);
				}
				id = atoi(ia->word_buffers[WI_MOD]);

				for (i = 0, index = -1; i < io_c->table.count; ++i) {
					if (io_c->table.rows[i].id == id) {
						index = i;
						break;
					}
				}
				if (index == -1) {
					fprintf(stderr, "URL ID %d could not be found.\n", id);
					exit(-1);
				}

				if (ia->word_buffers[WI_TITLE] != NULL) {
					strcpyt(io_c->table.rows[index].title,
					        ia->word_buffers[WI_TITLE],
					        TITLE_S, -1);
				}
				if (ia->word_buffers[WI_COMMENT] != NULL) {
					strcpyt(io_c->table.rows[index].comment,
					        ia->word_buffers[WI_COMMENT],
					        COMMENT_S, -1);
				}
				if (ia->word_buffers[WI_TAG] != NULL) {
					if (strstr(ia->word_buffers[WI_TAG], " ") == NULL) {
						UpdateRowTags(io_c, index, ia);
					} else {
						char* curr;
						curr = strtok(ia->word_buffers[WI_TAG], " ");
						for (i = 0; curr != NULL;) {
							UpdateRowTags(io_c, index, ia);
							curr = strtok(0, " ");
						}
					}
				}
				GetCurrentDateTime(&io_c->table.rows[index].datetime);
			}
			break;
		case IM_REMOVE:
			{
				unsigned int  i;
				         int  id, index;
				         char confirmation;
				
				if (!isdigit(ia->word_buffers[WI_MOD][0])) {
					printf("Arg 1 must be the URL ID which you want to " \
					       "delete\n");
					exit(-1);
				}
				
				id = atoi(ia->word_buffers[WI_MOD]);
				
				for (i = 0, index = -1; i < io_c->table.count; ++i) {
					if (io_c->table.rows[i].id == id) {
						index = i;
						break;
					}
				}
				if (index == -1) {
					printf("URL ID %d could not be found.\n", id);
					exit(-1);
				}
				
				printf("Are you sure you want to delete row %d entitled '%s'? " \
				       "[Y/n] \n", id, io_c->table.rows[index].title);
				
				scanf("%c", &confirmation);
				if ((confirmation == 'y') || (confirmation == 'Y')) {
					io_c->table.rows[index].id = 0;
					io_c->table.count--;
				} else {
					exit(0);
				}
			}
			break;
		case IM_LIST:
			{
				unsigned int i;
				if (ia->word_buffers[WI_MOD] != NULL) {
					if (stricmp(ia->word_buffers[WI_MOD], "all") == 0) {
						for (i = 0; i < io_c->table.count; ++i) {
							if (io_c->table.rows[i].id == 0) continue;
							PrintRow(io_c->table.rows[i], io_c->tags);
						}
					} else {
						char* ssPtr;
						for (i = 0; i < io_c->table.count; ++i) {
							if (io_c->table.rows[i].id == 0) continue;
							ssPtr = stristr(io_c->table.rows[i].title,
							                ia->word_buffers[WI_MOD]);
							if (ssPtr != NULL) {
								PrintRow(io_c->table.rows[i], io_c->tags);
							}
						}
					}
					return;
				}
				
				if (ia->word_buffers[WI_TAG] != NULL) {
					/* (Contains multiple values) */
					if (strstr(ia->word_buffers[WI_TAG], " ") != NULL) {
						char* curr;
						int freq;
						unsigned int* tmp;
						
						for (i = 0, freq = 0;
						     i < strlen(ia->word_buffers[WI_TAG]); ++i) {
							if (ia->word_buffers[WI_TAG][i] == ' ') freq++;
						}
						tmp = malloc(sizeof(int) *  (freq + 1));
						memset(tmp, 0, sizeof(int)* (freq + 1));
						
						i = 0;
						curr = strtok(ia->word_buffers[WI_TAG], " ");
						while (curr != NULL) {
							if (isdigit(curr[0])) { 
								tmp[i] = atoi(curr);
							} else {
								tmp[i] = GetTagID(io_c->tags, curr);
								if (tmp[i] == 0) {
									printf("Could not find tag '%s'\n", curr);
									exit(-1);
								}
							}
							curr = strtok(0, " ");
						}
						PrintWithTagIDs(tmp, freq, io_c->table.rows, io_c->table.count);
						free(tmp);
					/* (Single Value) */
					} else {
						char* tg = ia->word_buffers[WI_TAG];
						int id;
						
						if (isdigit(tg[0])) { 
							id = atoi(tg);
						} else {
							id = GetTagID(io_c->tags, tg);
							if (id == 0) {
								printf("Could not find tag '%s'\n", tg);
								exit(-1);
							}
						}
						{
							unsigned int tmp[1] = { id };
							PrintWithTagIDs(tmp, 1, io_c->table.rows, io_c->table.count);
						}
					}
				}
				
			}
			break;
		case IM_OPEN:
			{
				char buffer[9 + S_ADDR_S];
				char* url;
				int i, result, id, len;
				
				id = atoi(ia->word_buffers[WI_MOD]);
				for (i = 0, url = NULL; i < io_c->table.count; ++i) {
					if (io_c->table.rows[i].id == id) {
						if (io_c->table.rows[i].url.long_url == true) {
							url = io_c->table.rows[i].url.address.l;
						} else {
							url = io_c->table.rows[i].url.address.s;
						}
						break;
					}
				}
				
				if (url == NULL) {
					printf("Could not find row entry with this ID\n");
					exit(-1);
				}
				
				len = Min(9 + strlen(url), 9 + S_ADDR_S - 1);
				sprintf(buffer, "xdg-open %s", url);
				buffer[len] = 0;
				result = system(buffer);
				if (result != 0) {
					printf("Could not open URL\n");
					exit(-1);
				}
			}
			break;
		case IM_TAG_ADD:
			{
				if (!isdigit(ia->word_buffers[WI_MOD][0])) {
					ValidateTagName(ia->word_buffers, WI_MOD);
				} else {
					printf("Tag names cannot begin with a number.\n");
					exit(-1);
				}
				
				strcpy(io_c->tags.tags[io_c->tags.count].name,
				       ia->word_buffers[WI_MOD]);
				io_c->tags.tags[io_c->tags.count++].id = io_c->tags.next_UID++;
			}
			break;
		case IM_TAG_ADD_TO_ENTRY:
			{
				unsigned int i;
				unsigned int urlId,    tagIndex;
				         int urlIndex, freeTagIndex;
				
				if (isdigit(ia->word_buffers[WI_MOD][0])) {
					urlId = atoi(ia->word_buffers[WI_MOD]);
				} else {
					printf("arg 1 must be an URL id.\n");
					exit(-1);
				}
				
				if (!isdigit(ia->word_buffers[WI_TAG][0])) {
					ValidateTagName(ia->word_buffers, WI_TAG);
				}
				tagIndex = GetInputTagIndex(io_c, ia->word_buffers, WI_TAG);
				
				{
					for (i = 0, urlIndex = -1; i < io_c->table.count; ++i) {
						if (io_c->table.rows[i].id == urlId) {
							urlIndex = i;
							break;
						}
					}
					if (urlIndex == -1) {
						printf("Could not find url with ID of %d\n", urlId);
						exit(-1);
					}
				}
				{
					int full;
					
					full = true;
					for (i = 0, freeTagIndex = -1; i < ROW_TAG_C; ++i) {
						if (io_c->table.rows[urlIndex].tag_ids[i] == 0 &&
						    freeTagIndex == -1) {
							full = false;
							freeTagIndex = i;
						} else if (io_c->table.rows[urlIndex].tag_ids[i] ==
						           io_c->tags.tags[tagIndex].id) {
							printf("URL entry is already tagged with %s\n",
							       io_c->tags.tags[tagIndex].name);
							exit(0);
						}
					}
					
					if (full == true) {
						printf("Cannot add anymore tags to this url entry.\n");
						exit(-1);
					}
				}
				
				io_c->table.rows[urlIndex].tag_ids[freeTagIndex] = 
					io_c->tags.tags[tagIndex].id;
				GetCurrentDateTime(&io_c->table.rows[urlIndex].datetime);
			}
			break;
		case IM_TAG_RENAME:
			{
				unsigned int index;
				
				if (isdigit(ia->word_buffers[WI_TAG][0])) {
					printf("New tag-name cannot begin with a number");
				}
				
				index = GetInputTagIndex(io_c, ia->word_buffers, WI_MOD);
				strcpy(io_c->tags.tags[index].name, ia->word_buffers[WI_TAG]);
			}
			break;
		case IM_TAG_REMOVE:
			{
				unsigned int index, i, j;
				char confirmation;
				
				if (isdigit(ia->word_buffers[WI_MOD][0]) == false) {
					ValidateTagName(ia->word_buffers, WI_MOD);
				}
				index = GetInputTagIndex(io_c, ia->word_buffers, WI_MOD);
				printf("Are you sure want want to remove tag '%s'? [Y/n] \n",
				       io_c->tags.tags[index].name);
				confirmation = 0;
				scanf("%c", &confirmation);
				if ((confirmation == 'y') || (confirmation == 'Y')) {
					for (i = 0; i < io_c->table.count; ++i) {
						for (j = 0; j < ROW_TAG_C; ++j) {
							if (io_c->table.rows[i].tag_ids[j] == io_c->tags.tags[index].id) {
								io_c->table.rows[i].tag_ids[j] = 0;
								GetCurrentDateTime(&io_c->table.rows[i].datetime);
							}
						}
					}
					io_c->tags.tags[index].id = 0;
				} else {
					exit(-1);
				}
			}
			break;
		case IM_TAG_LIST:
			{
				unsigned int i;
				
				if (stricmp(ia->word_buffers[WI_MOD], "all") != 0) {
					printf("Only \"all\" can be used to list tags\n");
					exit(-1);
				}
				
				for (i = 0; i < io_c->tags.count; ++i) {
					if (io_c->tags.tags[i].id == 0) {
						continue;
					}
					printf("%d] %s\n",
					       io_c->tags.tags[i].id, io_c->tags.tags[i].name);
				}
			}
			break;
	}
}



static char*
stristr(const char* a, const char* b)
{
	const char* p1 = a;
	const char* p2 = b;
	const char* r = *p2 == 0 ? a : 0;
	
	while (*p1 != 0 && * p2 != 0) {
		if (toupper((unsigned char) *p1) == toupper((unsigned char) *p2)) {
			if (r == 0) {
				r = p1;
			}
			p2++;
		} else {
			p2 = b;
			if (r != 0) {
				p1 = r + 1;
			}
			
			if (toupper((unsigned char)*p1) == toupper((unsigned char)*p2)) {
				r = p1;
				p2++;
			} else {
				r = 0;
			}
		}
		
		p1++;
	}
	
	return *p2 == 0 ? (char*)r : 0;
}

static int
stricmp(const char* a, const char* b)
{
	if (a == NULL || b == NULL) {
		return(false);
	}
	while(toupper(*a) && (toupper(*a) == toupper(*b))) {
		a++;
		b++;
	}
	
	return *(const char*) a - *(const char*) b;
}

static char*
strcpyt(char* d, char* s, unsigned int m, int l)
{
	int i;
	
	/* Sometimes I only want to copy a substring, other times the whole
	 * string. In the latter case, it is sometimes annoying to have to 
	 * call strlen and input a large variable name. Instead I can pass in
	 * -1 and this will save me the time. */
	if (l == -1) {
		l = strlen(s);
	}
	if (l >= m) {
		l = m - 1;
	}
	
	strncpy(d, s, l);
	if ((l >= m) && (l > 4)) {
		l = m - 4;
		for (i = l; i < l + 3; ++i) {
			d[i] = '.';
		}
		/* TODO: test */
		d[m - 1] = '\0';
	} else {
		/* TODO: test */
		d[l] = '\0';
	}
	
	return NULL;
}

static int
RowHasTagId(Row r, unsigned int id)
{
	unsigned int i;
	for (i = 0; i < ROW_TAG_C; ++i) {
		if (r.tag_ids[i] == id) {
			return true;
		}
	}
	
	return false;
}

static void
PrintWithTagIDs(unsigned int* tagIds, unsigned int tc,
               Row* r, unsigned int rc)
{
	unsigned int i, j;
	for (i = 0; i < rc; ++i) {
		for (j = 0; j < tc; ++j) {
			if (RowHasTagId(r[i], tagIds[j]) == true) {
				if (r[i].url.long_url == true) {
					printf("%d. %s\n\t > %s\n",
					       i, r[i].title, r[i].url.address.l);
				} else {
					printf("%d. %s\n\t > %s\n",
					       i, r[i].title, r[i].url.address.s);
				}
			}
		}
	}
}

static char*
GetTagName(Tags t, unsigned int id)
{
	unsigned int i;
	for (i = 0; i < ROW_TAG_C; ++i) {
		if (t.tags[i].id == id) {
			return t.tags[i].name;
		}
	}
	
	return NULL;
}

static unsigned int
GetTagID(Tags t, char* s)
{
	unsigned int i;
	for (i = 0; i < ROW_TAG_C; ++i) {
		if (strcmp(t.tags[i].name, s) == 0) {
			return t.tags[i].id;
		}
	}
	
	return 0;
}

static void
PrintRow(Row r, Tags tg)
{
	unsigned int i, hasTags;
	if (r.url.long_url == true) { 
		printf("%3d. %s\n\t > %s\n",
		       r.id, r.title, r.url.address.l);
	} else {
		printf("%3d. %s\n\t > %s\n",
		       r.id, r.title, r.url.address.s);
	}
	
	for (i = 0, hasTags = false; i < ROW_TAG_C; ++i) {
		if (r.tag_ids[i] == 0) continue;
		hasTags = true;
		break;
	}
	if (hasTags) {
		printf("\t |");
	}
	for (i = 0; i < ROW_TAG_C; ++i) {
		if (r.tag_ids[i] == 0) continue;
		printf(" %s |", GetTagName(tg, r.tag_ids[i]));
	}
	if (hasTags) {
		printf("\n");
	}
}

static size_t
CURLBuildPage(char* b, size_t s, size_t c, void* d)
{
	CURLData* cd;
	int i;
	
	if (d == NULL){
		return s * c;
	}
	
	cd = (CURLData*) d;
	if (cd->contents != NULL) {
		if ((strstr(cd->contents, "</title>")  != NULL) ||
		    (strstr(cd->contents, "</header>") != NULL)) {
			return s * c;
		}
	}
	
	for (i = 0; i < c; ++i) {
		cd->contents[cd->index] = b[i];
		cd->index++;
	}
	
	return s * c;
}

static void
GetPageTitle(char* contents, char* o_buffer)
{
	char* startPos, *endPos;
	int len;
	
	if (contents == NULL || strlen(contents) < 1) {
		printf("No webpage contents read\n");
	}
	
	startPos = strstr(contents, "<title>");
	endPos   = strstr(contents, "</title>");
	if (startPos == NULL) { 
		printf("No <title> tag\n");
	} 
	if (endPos == NULL) {
		printf("No </title> tag\n");
	}
	startPos += strlen("<title>");
	assert(startPos < endPos);
	
	len = endPos - startPos;
	strcpyt(o_buffer, startPos, TITLE_S, len);
}

static CURLData*
GetWebpage(char* url)
{
	CURLData* result;
	CURL*     curl;
	CURLcode  code;
	
	result = malloc(sizeof(CURLData));
	memset(result, 0, sizeof(CURLData));
	
	{
		/* NOTE: This is the biggest design problem. It first reads the page to
		 * determine how much needs to be alloced, but this just doubles the
		 * amount of time spent downloading. This is always going to be *much*
		 * longer than just reallocating every CURLBuildPage() call... */
		long size;
		curl = curl_easy_init();
		if (curl) {
			CURLcode res;
			curl_easy_setopt(curl, CURLOPT_URL, url);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CURLBuildPage);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
			res = curl_easy_perform(curl);
			if (res == CURLE_OK) {
				res = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T , &size);
			}
			curl_easy_cleanup(curl);
		}
		result->contents_length = size + 1;
		result->contents = malloc(result->contents_length);
		memset(result->contents, 0, result->contents_length);
	}
	
	curl = curl_easy_init();
	if (!curl) {
		printf("Could not init CURL.");
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, result);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CURLBuildPage);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	
	code = curl_easy_perform(curl);
	if (code != CURLE_OK) {
		fprintf(stderr,
		        "Could not download page: %s\n",
		        curl_easy_strerror(code));
		free(result->contents);
		exit(0);
	}
	
	curl_easy_cleanup(curl);
	return result;
}

static void
ValidateTagName(char* io_buffer[], const unsigned int index)
{
	char buffer[TAG_NAME_S];
	int i;
	
	assert(io_buffer[index]);
	memset(buffer, 0, sizeof(char) * TAG_NAME_S);
	if (isdigit(io_buffer[index][0])/* == true*/) {
		printf("Invalid tag name. Tag names cannot start with a number.\n");
		exit(0);
	}
	else if ((stricmp(io_buffer[index], "add") == 0) ||
	         (stricmp(io_buffer[index], "update") == 0) ||
	         (stricmp(io_buffer[index], "rename") == 0) ||
	         (stricmp(io_buffer[index], "remove") == 0)) {
		printf("Invalid tag name. Tag names cannot be set to reserved terms\n");
		exit(0);
	}
	
	if (strstr(io_buffer[index], " ") != NULL) {
		for (i = 0; i < strlen(io_buffer[index]); ++i) {
			char c = io_buffer[index][i];
			if (c == ' ') {
				buffer[i] = '-';
			} else {
				buffer[i] = c;
			}
		}
	}
	
	if (strlen(buffer) > 0) {
		strcpy(io_buffer[index], buffer);
	}
}

static unsigned int
GetInputTagIndex(Core* c, char* buffers[], const unsigned int inputIndex)
{
	int result;
	int i;
	
	result = 0;
	if (buffers[inputIndex] != NULL) {
		if (isdigit(buffers[inputIndex][0])) {
			const int value = atoi(buffers[inputIndex]);
			for (i = 0, result = 0; i < c->tags.count; ++i) {
				if (c->tags.tags[i].id == value) {
					result = i;
					break;
				}
			}
		} else {
			for (i = 0, result = 0; i < c->tags.count; ++i) {
				if (stricmp(c->tags.tags[i].name, buffers[inputIndex]) == 0) {
					result = i;
					break;
				}
			}
		}
	} else {
		printf("Invalid args\n");
		exit(0);
	}
	
	return result;
}

static void
GetCurrentDateTime(DateTime* o_dt)
{
	struct tm* t;
	time_t now;
	
	now = time(0);
	t = localtime(&now);
	strftime(o_dt->last_updated, 19, "%F %T", t);
	
	if (sscanf
	(
		o_dt->last_updated,
		"%d-%d-%d %d:%d:%d",
		&o_dt->d_y, &o_dt->d_m, &o_dt->d_d,
		&o_dt->t_h, &o_dt->t_m, &o_dt->t_s
	) < 1) {
		printf("Could not parse date and time\n");
	}
}

static void
UpdateRowTags(Core* io_c, unsigned int rowIndex, InputArgs* ia)
{
	char* tagInput;
	int tagId, tagIndexInRow;
	
	tagInput = ia->word_buffers[WI_TAG];
	{
		if (isdigit(tagInput[0])) {
			tagId = atoi(tagInput);
			if (tagId < 1) {
				printf("Invalid tag ID (%s)\n", tagInput);
			}
		} else {
			int index;
			ValidateTagName(ia->word_buffers, WI_TAG);
			index = GetInputTagIndex(io_c, ia->word_buffers, WI_TAG);
			tagId = io_c->tags.tags[index].id;
		}
	}
	{
		unsigned int i, full;
		
		for (i = 0, full = true, tagIndexInRow = -1; i < ROW_TAG_C; ++i) {
			if (io_c->table.rows[rowIndex].tag_ids[i] == 0 &&
			    tagIndexInRow == -1) {
				full = false;
				tagIndexInRow = i;
			} else if (io_c->table.rows[rowIndex].tag_ids[i] == tagId) {
				char confirmation;
				
				full = false;
				tagIndexInRow = i;
				printf("Are you sure you want to remove tag %s from row " \
				       "%d? [Y/n] \n", GetTagName(io_c->tags, tagId),  tagId);
				
				scanf("%c", &confirmation);
				if ((confirmation == 'y') || (confirmation == 'Y')) {
					tagId = 0;
				} else {
					exit(0);
				}
				break;
			}
		}
		
		if (full == true) {
			printf("Cannot add anymore tags to this url entry.\n");
			exit(0);
		}
	}
	
	io_c->table.rows[rowIndex].tag_ids[tagIndexInRow] = tagId;
}

static void
GetConfigPath(char* o_buffer)
{
	if (strstr(cache_dir, "~/") != NULL) {
		const char* homedir;
		unsigned int index = 0, i;
		
		if ((homedir = getenv("HOME")) == NULL) {
			homedir = getpwuid(getuid())->pw_dir;
		}
		
		for (i = 0; i < strlen(cache_dir); ++i, ++index) {
			if (cache_dir[i] == '~') {
				unsigned int j;
				for (j = 0; j < strlen(homedir); ++j, ++index) {
					o_buffer[index] = homedir[j];
				}
				index--;
			} else {
				o_buffer[index] = cache_dir[i];
			}
		}
	} else {
		strcpy(o_buffer, cache_dir);
	}
}

int
main(int argc, char* args[])
{
	InputArgs inputArgs;
	Core core;
	
	memset(&inputArgs, 0, sizeof(InputArgs));
	if (argc > 1) {
		args = &args[1];
		argc--;
		if (strcmp(args[0], "tag") == 0) {
			inputArgs = ParseTagInput(args, argc);
		} else {
			inputArgs = ParseEntryInput(args, argc);
		}
	} else {
		printf("No args provided.\n");
	}
	core = ReadJSON();
	ProcessCommand(&core, &inputArgs);
	if (WriteJSON(&core) < 1) {
		printf("Could not save to JSON");
	}
	
	{
		unsigned int i;
		for (i = 0; i < core.table.count; ++i) {
			if (core.table.rows[i].url.long_url == true) {
				free(core.table.rows[i].url.address.l);
			}
		}
		free(core.table.rows);
		free(core.tags.tags);
	}
	
	return 0;
}
