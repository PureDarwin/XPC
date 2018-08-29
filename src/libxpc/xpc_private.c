#include <xpc/xpc.h>
#include <xpc/private.h>
#include <sys/codesign.h>
#include <ctype.h>
#include <errno.h>

int _xpc_runtime_is_app_sandboxed(void)
{
	return 0;
}

kern_return_t xpc_domain_server(mach_msg_header_t *request, mach_msg_header_t *reply) {
	// Not implemented.
	return KERN_FAILURE;
}

#pragma mark -

static char *entitlements_plist_for_pid(pid_t pid) {
	char fakeheader[8];
	int ret = csops(pid, CS_OPS_ENTITLEMENTS_BLOB, fakeheader, sizeof(fakeheader));
	if (ret == -1 && errno != ERANGE) {
		return NULL;
	}

	uint32_t required_length = ((uint32_t *)&fakeheader)[1];
	required_length = ntohl(required_length);

	if (required_length != 0) {
		char *blob = calloc(1, required_length);
		ret = csops(pid, CS_OPS_ENTITLEMENTS_BLOB, blob, required_length);
		if (ret == -1) {
			return NULL;
		}

		// The first 8 bytes of the blob are a binary header; skip it.
		// The rest of the blob is an XML-format plist.
		char *plist = strdup(blob + 8);
		free(blob);
		return plist;
	} else {
		// The process has no entitlements.
		return strdup("");
	}
}

static inline void skip_whitespace(char *ptr) {
	while (isblank(*ptr) || *ptr == '\r' || *ptr == '\n') ptr++;
}

static inline char *strdup_until(char *ptr, const char *delimiter) {
	char *delimiter_location = strstr(ptr, delimiter);
	char *value = strndup(ptr, delimiter_location - ptr);
	ptr = delimiter_location;
	return value;
}

static inline bool scan_string(char *ptr, const char *expected) {
	size_t size = strlen(expected);
	if (strncmp(ptr, expected, size) != 0) return false;
	ptr += size;
	return true;
}

// This implementation only supports arrays and dictionaries of strings and booleans.
// Other plist types are not implemented, and will result in failure of parsing if encountered.
// Arrays and dictionaries can be nested arbitrarily deep, however.
static xpc_object_t plist_to_array(char *ptr);
static xpc_object_t plist_to_dict(char *ptr);

static xpc_object_t plist_to_array(char *ptr) {
	const char *array_tag = "<array>";
	const size_t array_tag_len = strlen(array_tag);

	skip_whitespace(ptr);
	if (!scan_string(ptr, array_tag)) return NULL;
	skip_whitespace(ptr);

	const char *slash_array = "</array>";
	const size_t slash_array_len = strlen(slash_array);
	const char *dict_tag = "<dict>";
	const size_t dict_tag_len = strlen(dict_tag);
	const char *string_tag = "<string>";
	const size_t string_tag_len = strlen(string_tag);
	const char *slash_string = "</string>";
	const size_t slash_string_len = strlen(slash_string);
	const char *true_tag = "<true/>";
	const size_t true_tag_len = strlen(true_tag);
	const char *false_tag = "<false/>";
	const size_t false_tag_len = strlen(false_tag);

	size_t buffer_size = 16, object_count = 0;
	xpc_object_t *values = calloc(buffer_size, sizeof(const xpc_object_t));

	while (strncmp(ptr, slash_array, slash_array_len) != 0) {
		skip_whitespace(ptr);

		if (object_count == buffer_size) {
			buffer_size *= 2;
			values = realloc(values, buffer_size * sizeof(const xpc_object_t));
		}

		if (strncmp(ptr, string_tag, string_tag_len) == 0) {
			ptr += string_tag_len;

			char *value_raw = strdup_until(ptr, slash_string);
			values[object_count] = xpc_string_create(value_raw);
			free(value_raw);
		} else if (strncmp(ptr, true_tag, true_tag_len)) {
			values[object_count] = xpc_bool_create(true);
		} else if (strncmp(ptr, false_tag, false_tag_len)) {
			values[object_count] = xpc_bool_create(false);
		} else if (strncmp(ptr, array_tag, array_tag_len)) {
			values[object_count] = plist_to_array(ptr);
			if (values[object_count] == NULL) goto failure;
		} else if (strncmp(ptr, dict_tag, dict_tag_len)) {
			values[object_count] = plist_to_dict(ptr);
			if (values[object_count] == NULL) goto failure;
		} else {
			goto failure;
		}
	}

	if (!scan_string(ptr, "</array>")) goto failure;

	xpc_object_t array = xpc_array_create(values, object_count);
	free(values);
	return array;

failure:
	free(values);
	return NULL;
}

static xpc_object_t plist_to_dict(char *ptr) {
	const char *dict_tag = "<dict>";
	const size_t dict_tag_len = strlen(dict_tag);

	skip_whitespace(ptr);
	if (!scan_string(ptr, dict_tag)) return NULL;
	skip_whitespace(ptr);

	const char *array_tag = "<array>";
	const size_t array_tag_len = strlen(array_tag);
	const char *slash_dict = "</dict>";
	const size_t slash_dict_len = strlen(slash_dict);
	const char *string_tag = "<string>";
	const size_t string_tag_len = strlen(string_tag);
	const char *slash_string = "</string>";
	const size_t slash_string_len = strlen(slash_string);
	const char *true_tag = "<true/>";
	const size_t true_tag_len = strlen(true_tag);
	const char *false_tag = "<false/>";
	const size_t false_tag_len = strlen(false_tag);

	size_t buffer_size = 16, object_count = 0;
	const char **keys = calloc(buffer_size, sizeof(const char *));
	xpc_object_t *values = calloc(buffer_size, sizeof(const xpc_object_t));

	while (strncmp(ptr, slash_dict, slash_dict_len) != 0) {
		skip_whitespace(ptr);

		if (object_count == buffer_size) {
			buffer_size *= 2;
			keys = realloc(keys, buffer_size * sizeof(const char *));
			values = realloc(values, buffer_size * sizeof(const xpc_object_t));
		}

		if (!scan_string(ptr, "<key>")) goto failure;
		keys[object_count] = strdup_until(ptr, "</key>");
		skip_whitespace(ptr);

		if (strncmp(ptr, string_tag, string_tag_len) == 0) {
			ptr += string_tag_len;

			char *value_raw = strdup_until(ptr, slash_string);
			values[object_count] = xpc_string_create(value_raw);
			free(value_raw);

			if (!scan_string(ptr, slash_string)) goto failure;
		} else if (strncmp(ptr, true_tag, true_tag_len)) {
			values[object_count] = xpc_bool_create(true);
		} else if (strncmp(ptr, false_tag, false_tag_len)) {
			values[object_count] = xpc_bool_create(false);
		} else if (strncmp(ptr, array_tag, array_tag_len)) {
			values[object_count] = plist_to_array(ptr);
			if (values[object_count] == NULL) goto failure;
		} else if (strncmp(ptr, dict_tag, dict_tag_len)) {
			values[object_count] = plist_to_dict(ptr);
			if (values[object_count] == NULL) goto failure;
		} else {
			goto failure;
		}

		object_count++;
		skip_whitespace(ptr);
	}

	if (!scan_string(ptr, "</dict>")) goto failure;

	xpc_object_t dict = xpc_dictionary_create(keys, values, object_count);
	free(keys);
	free(values);
	return dict;

failure:
	free(keys);
	free(values);
	return NULL;
}

xpc_object_t xpc_copy_entitlements_for_pid(pid_t pid) {
	char *plist_buffer = entitlements_plist_for_pid(pid);
	char *plist = plist_buffer;
	if (plist == NULL) {
		// Error!
		return NULL;
	}

	if (strlen(plist) == 0) {
		// No entitlements in the binary for the pid given, return empty dictionary.
		return xpc_dictionary_create(NULL, NULL, 0);
	}

	skip_whitespace(plist);
	if (!scan_string(plist, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>")) goto failure;
	skip_whitespace(plist);
	if (!scan_string(plist, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">")) goto failure;
	skip_whitespace(plist);

	if (!scan_string(plist, "<plist version=\"1.0\">")) goto failure;
	xpc_object_t entitlements = plist_to_dict(plist);
	skip_whitespace(plist);
	if (!scan_string(plist, "</plist>")) goto failure;

	// Any data past the </plist> is ignored.
	free(plist_buffer);
	return entitlements;

failure:
	free(plist_buffer);
	return NULL;
}
