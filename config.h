
static const char* cache_dir = "~/.config/sbm/";
static const char* cache_filename = "data.json";

/* These values can be changed to increase things such as the comment size.
 * Doing so will hurt the portability of the savefile. Decreasing sizes
 * could be an issue if not writing to a fresh config file. */
enum {
	ROW_TAG_C = 8,
	
	TITLE_S    = 64,
	COMMENT_S  = 256,
	S_ADDR_S   = 256,
	TAG_NAME_S = 32,
};
