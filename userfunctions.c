   /*******************************************************/
   /*      "C" Language Integrated Production System      */
   /*                                                     */
   /*            CLIPS Version 6.40  07/30/16             */
   /*                                                     */
   /*                USER FUNCTIONS MODULE                */
   /*******************************************************/

/*************************************************************/
/* Purpose: CLIPSVN. The svn-* functions give CLIPS          */
/*          programs access to the Subversion libraries.     */
/*                                                           */
/* Principal Programmer(s):                                  */
/*      Gary D. Riley                                        */
/*                                                           */
/* Contributing Programmer(s):                               */
/*      Ryan Johnston                                        */
/*                                                           */
/*************************************************************/

/***************************************************************************/
/*                                                                         */
/* Permission is hereby granted, free of charge, to any person obtaining   */
/* a copy of this software and associated documentation files (the         */
/* "Software"), to deal in the Software without restriction, including     */
/* without limitation the rights to use, copy, modify, merge, publish,     */
/* distribute, and/or sell copies of the Software, and to permit persons   */
/* to whom the Software is furnished to do so.                             */
/*                                                                         */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS */
/* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF              */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT   */
/* OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY  */
/* CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES */
/* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN   */
/* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF */
/* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.          */
/*                                                                         */
/***************************************************************************/

/* The APR headers decide APR_PATH_MAX from the feature macros that are set
   before they are read. The CLIPS makefile compiles with -std=c99, which
   hides PATH_MAX. So the extensions are asked for here, first. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1

#include "clips.h"

#include <apr_general.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_time.h>
#include <apr_strings.h>

#include <svn_version.h>
#include <svn_types.h>
#include <svn_error.h>
#include <svn_pools.h>
#include <svn_string.h>
#include <svn_hash.h>
#include <svn_io.h>
#include <svn_time.h>
#include <svn_props.h>
#include <svn_opt.h>
#include <svn_auth.h>
#include <svn_config.h>
#include <svn_dirent_uri.h>
#include <svn_path.h>
#include <svn_client.h>
#include <svn_wc.h>
#include <svn_ra.h>
#include <svn_repos.h>
#include <svn_fs.h>
#include <svn_checksum.h>
#include <svn_utf.h>
#include <svn_diff.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void UserFunctions(Environment *);

/***************************************************************************/
/*                                                                         */
/* The Subversion version.                                                 */
/*                                                                         */
/* Subversion 1.12 is the oldest release that this file supports. The 1.14 */
/* series has the same C API. The version in the headers is the version    */
/* that the wrappers are compiled against. The function svn-built-version  */
/* reports that version to the program.                                    */
/*                                                                         */
/***************************************************************************/

#define CLIPSVN_SVN_VERSION_NUM \
	(SVN_VER_MAJOR * 10000 + SVN_VER_MINOR * 100 + SVN_VER_PATCH)

#if CLIPSVN_SVN_VERSION_NUM < 11200
#error "this Subversion is too old for CLIPSVN: Subversion 1.12 or later is needed. Build the vendored library with 'make', or point at a newer installation."
#endif

/***************************************************************************/
/*                                                                         */
/* Reports.                                                                */
/*                                                                         */
/* Each wrapper reports a rejected call in the same way. It writes the     */
/* CLIPS name of the function and the reason to stderr. Then it returns    */
/* FALSE to the caller. No function in this file causes a CLIPS error. A   */
/* rejected call is a value that the caller can test. This is why the      */
/* standard pattern is (if (bind ?r (svn-...)) then ...).                  */
/*                                                                         */
/***************************************************************************/

static Environment *SvnEnv = NULL;
static apr_pool_t *SvnRootPool = NULL;

/* The last error that Subversion reported, for svn-error-last. */
static char SvnLastMessage[2048] = "";
static char SvnLastName[128] = "";
static apr_status_t SvnLastCode = 0;

static void SvnFail(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  const char *msg)
{
	WriteString(theEnv, STDERR, fname);
	WriteString(theEnv, STDERR, ": ");
	WriteString(theEnv, STDERR, msg);
	WriteString(theEnv, STDERR, "\n");
	returnValue->lexemeValue = FalseSymbol(theEnv);
}

/* Records an svn_error_t chain as one message: the messages of the links,
   from the outside in, without the tracing links and without repeats. The
   chain is cleared. */
static void SvnRecordError(
  svn_error_t *err)
{
	svn_error_t *e;
	const char *last = NULL;
	size_t used = 0;
	char buf[256];

	SvnLastMessage[0] = '\0';
	SvnLastCode = err->apr_err;
	{
		const char *name = svn_error_symbolic_name(err->apr_err);
		snprintf(SvnLastName, sizeof SvnLastName, "%s", name != NULL ? name : "");
	}

	err = svn_error_purge_tracing(err);

	for (e = err; e != NULL; e = e->child)
	{
		const char *msg;

		if (e->message != NULL && e->message[0] != '\0')
		{
			msg = e->message;
		}
		else
		{
			msg = svn_strerror(e->apr_err, buf, sizeof buf);
		}

		if (last != NULL && strcmp(last, msg) == 0)
		{
			continue;
		}

		if (used > 0 && used < sizeof SvnLastMessage - 3)
		{
			used += (size_t) snprintf(SvnLastMessage + used, sizeof SvnLastMessage - used, "; ");
		}
		if (used < sizeof SvnLastMessage - 1)
		{
			used += (size_t) snprintf(SvnLastMessage + used, sizeof SvnLastMessage - used, "%s", msg);
		}
		if (used >= sizeof SvnLastMessage)
		{
			used = sizeof SvnLastMessage - 1;
		}
		last = msg;
	}

	svn_error_clear(err);
}

/* The same report, with the failure data from Subversion: the messages of
   the error chain, and the symbolic name of its code. */
static void SvnFailError(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  svn_error_t *err)
{
	SvnRecordError(err);

	WriteString(theEnv, STDERR, fname);
	WriteString(theEnv, STDERR, ": ");
	WriteString(theEnv, STDERR, SvnLastMessage[0] != '\0' ? SvnLastMessage : "the call failed and Subversion reports no message");
	if (SvnLastName[0] != '\0')
	{
		WriteString(theEnv, STDERR, " (");
		WriteString(theEnv, STDERR, SvnLastName);
		WriteString(theEnv, STDERR, ")");
	}
	WriteString(theEnv, STDERR, "\n");
	returnValue->lexemeValue = FalseSymbol(theEnv);
}

/***************************************************************************/
/*                                                                         */
/* Enumerations.                                                           */
/*                                                                         */
/* Each value that Subversion names with a C constant crosses into CLIPS   */
/* as the symbol with that C name: svn_node_file, svn_depth_infinity,      */
/* svn_wc_status_modified. A table maps each name to its value and back.   */
/*                                                                         */
/***************************************************************************/

struct SvnEnumEntry
{
	const char *name;
	long long value;
};

static const char *SvnEnumName(
  const struct SvnEnumEntry *table,
  long long value)
{
	int i;

	for (i = 0; table[i].name != NULL; i++)
	{
		if (table[i].value == value)
		{
			return table[i].name;
		}
	}

	return NULL;
}

#define E(name) { #name, name }

static const struct SvnEnumEntry SvnNodeKindNames[] = {
	E(svn_node_none), E(svn_node_file), E(svn_node_dir), E(svn_node_unknown),
	E(svn_node_symlink), { NULL, 0 }
};

static const struct SvnEnumEntry SvnDepthNames[] = {
	E(svn_depth_unknown), E(svn_depth_exclude), E(svn_depth_empty), E(svn_depth_files),
	E(svn_depth_immediates), E(svn_depth_infinity),
	/* The words that the command line uses. */
	{ "unknown", svn_depth_unknown }, { "exclude", svn_depth_exclude },
	{ "empty", svn_depth_empty }, { "files", svn_depth_files },
	{ "immediates", svn_depth_immediates }, { "infinity", svn_depth_infinity },
	{ NULL, 0 }
};

static const struct SvnEnumEntry SvnRevisionKindNames[] = {
	E(svn_opt_revision_unspecified), E(svn_opt_revision_number), E(svn_opt_revision_date),
	E(svn_opt_revision_committed), E(svn_opt_revision_previous), E(svn_opt_revision_base),
	E(svn_opt_revision_working), E(svn_opt_revision_head),
	/* The words that the command line uses. */
	{ "HEAD", svn_opt_revision_head }, { "BASE", svn_opt_revision_base },
	{ "COMMITTED", svn_opt_revision_committed }, { "PREV", svn_opt_revision_previous },
	{ "WORKING", svn_opt_revision_working },
	{ NULL, 0 }
};

static const struct SvnEnumEntry SvnStatusKindNames[] = {
	E(svn_wc_status_none), E(svn_wc_status_unversioned), E(svn_wc_status_normal),
	E(svn_wc_status_added), E(svn_wc_status_missing), E(svn_wc_status_deleted),
	E(svn_wc_status_replaced), E(svn_wc_status_modified), E(svn_wc_status_merged),
	E(svn_wc_status_conflicted), E(svn_wc_status_ignored), E(svn_wc_status_obstructed),
	E(svn_wc_status_external), E(svn_wc_status_incomplete), { NULL, 0 }
};

static const struct SvnEnumEntry SvnScheduleNames[] = {
	E(svn_wc_schedule_normal), E(svn_wc_schedule_add), E(svn_wc_schedule_delete),
	E(svn_wc_schedule_replace), { NULL, 0 }
};

static const struct SvnEnumEntry SvnTristateNames[] = {
	E(svn_tristate_false), E(svn_tristate_true), E(svn_tristate_unknown), { NULL, 0 }
};

static const struct SvnEnumEntry SvnConflictChoiceNames[] = {
	E(svn_wc_conflict_choose_postpone), E(svn_wc_conflict_choose_base),
	E(svn_wc_conflict_choose_theirs_full), E(svn_wc_conflict_choose_mine_full),
	E(svn_wc_conflict_choose_theirs_conflict), E(svn_wc_conflict_choose_mine_conflict),
	E(svn_wc_conflict_choose_merged), E(svn_wc_conflict_choose_unspecified),
	/* The words that the command line uses. */
	{ "postpone", svn_wc_conflict_choose_postpone }, { "base", svn_wc_conflict_choose_base },
	{ "theirs-full", svn_wc_conflict_choose_theirs_full }, { "mine-full", svn_wc_conflict_choose_mine_full },
	{ "theirs-conflict", svn_wc_conflict_choose_theirs_conflict }, { "mine-conflict", svn_wc_conflict_choose_mine_conflict },
	{ "working", svn_wc_conflict_choose_merged },
	{ NULL, 0 }
};

static const struct SvnEnumEntry SvnSummarizeKindNames[] = {
	E(svn_client_diff_summarize_kind_normal), E(svn_client_diff_summarize_kind_added),
	E(svn_client_diff_summarize_kind_modified), E(svn_client_diff_summarize_kind_deleted),
	{ NULL, 0 }
};

static const struct SvnEnumEntry SvnNotifyStateNames[] = {
	E(svn_wc_notify_state_inapplicable), E(svn_wc_notify_state_unknown),
	E(svn_wc_notify_state_unchanged), E(svn_wc_notify_state_missing),
	E(svn_wc_notify_state_obstructed), E(svn_wc_notify_state_changed),
	E(svn_wc_notify_state_merged), E(svn_wc_notify_state_conflicted),
	E(svn_wc_notify_state_source_missing), { NULL, 0 }
};

static const struct SvnEnumEntry SvnLockStateNames[] = {
	E(svn_wc_notify_lock_state_inapplicable), E(svn_wc_notify_lock_state_unknown),
	E(svn_wc_notify_lock_state_unchanged), E(svn_wc_notify_lock_state_locked),
	E(svn_wc_notify_lock_state_unlocked), { NULL, 0 }
};

static const struct SvnEnumEntry SvnNotifyActionNames[] = {
	E(svn_wc_notify_add), E(svn_wc_notify_copy), E(svn_wc_notify_delete),
	E(svn_wc_notify_restore), E(svn_wc_notify_revert), E(svn_wc_notify_failed_revert),
	E(svn_wc_notify_resolved), E(svn_wc_notify_skip), E(svn_wc_notify_update_delete),
	E(svn_wc_notify_update_add), E(svn_wc_notify_update_update),
	E(svn_wc_notify_update_completed), E(svn_wc_notify_update_external),
	E(svn_wc_notify_status_completed), E(svn_wc_notify_status_external),
	E(svn_wc_notify_commit_modified), E(svn_wc_notify_commit_added),
	E(svn_wc_notify_commit_deleted), E(svn_wc_notify_commit_replaced),
	E(svn_wc_notify_commit_postfix_txdelta), E(svn_wc_notify_blame_revision),
	E(svn_wc_notify_locked), E(svn_wc_notify_unlocked), E(svn_wc_notify_failed_lock),
	E(svn_wc_notify_failed_unlock), E(svn_wc_notify_exists), E(svn_wc_notify_changelist_set),
	E(svn_wc_notify_changelist_clear), E(svn_wc_notify_changelist_moved),
	E(svn_wc_notify_merge_begin), E(svn_wc_notify_foreign_merge_begin),
	E(svn_wc_notify_update_replace), E(svn_wc_notify_property_added),
	E(svn_wc_notify_property_modified), E(svn_wc_notify_property_deleted),
	E(svn_wc_notify_property_deleted_nonexistent), E(svn_wc_notify_revprop_set),
	E(svn_wc_notify_revprop_deleted), E(svn_wc_notify_merge_completed),
	E(svn_wc_notify_tree_conflict), E(svn_wc_notify_failed_external),
	E(svn_wc_notify_update_started), E(svn_wc_notify_update_skip_obstruction),
	E(svn_wc_notify_update_skip_working_only), E(svn_wc_notify_update_skip_access_denied),
	E(svn_wc_notify_update_external_removed), E(svn_wc_notify_update_shadowed_add),
	E(svn_wc_notify_update_shadowed_update), E(svn_wc_notify_update_shadowed_delete),
	E(svn_wc_notify_merge_record_info), E(svn_wc_notify_upgraded_path),
	E(svn_wc_notify_merge_record_info_begin), E(svn_wc_notify_merge_elide_info),
	E(svn_wc_notify_patch), E(svn_wc_notify_patch_applied_hunk),
	E(svn_wc_notify_patch_rejected_hunk), E(svn_wc_notify_patch_hunk_already_applied),
	E(svn_wc_notify_commit_copied), E(svn_wc_notify_commit_copied_replaced),
	E(svn_wc_notify_url_redirect), E(svn_wc_notify_path_nonexistent), E(svn_wc_notify_exclude),
	E(svn_wc_notify_failed_conflict), E(svn_wc_notify_failed_missing),
	E(svn_wc_notify_failed_out_of_date), E(svn_wc_notify_failed_no_parent),
	E(svn_wc_notify_failed_locked), E(svn_wc_notify_failed_forbidden_by_server),
	E(svn_wc_notify_skip_conflicted), E(svn_wc_notify_update_broken_lock),
	E(svn_wc_notify_failed_obstruction), E(svn_wc_notify_conflict_resolver_starting),
	E(svn_wc_notify_conflict_resolver_done), E(svn_wc_notify_left_local_modifications),
	E(svn_wc_notify_foreign_copy_begin), E(svn_wc_notify_move_broken),
	E(svn_wc_notify_cleanup_external), E(svn_wc_notify_failed_requires_target),
	E(svn_wc_notify_info_external), E(svn_wc_notify_commit_finalizing),
	E(svn_wc_notify_resolved_text), E(svn_wc_notify_resolved_prop), E(svn_wc_notify_resolved_tree),
	E(svn_wc_notify_begin_search_tree_conflict_details),
	E(svn_wc_notify_tree_conflict_details_progress),
	E(svn_wc_notify_end_search_tree_conflict_details), { NULL, 0 }
};

/* The names of the repository capabilities, as symbols and as the strings
   that svn_ra_has_capability takes. */
static const struct { const char *name; const char *value; } SvnCapabilityNames[] = {
	{ "SVN_RA_CAPABILITY_DEPTH", SVN_RA_CAPABILITY_DEPTH },
	{ "SVN_RA_CAPABILITY_MERGEINFO", SVN_RA_CAPABILITY_MERGEINFO },
	{ "SVN_RA_CAPABILITY_LOG_REVPROPS", SVN_RA_CAPABILITY_LOG_REVPROPS },
	{ "SVN_RA_CAPABILITY_PARTIAL_REPLAY", SVN_RA_CAPABILITY_PARTIAL_REPLAY },
	{ "SVN_RA_CAPABILITY_COMMIT_REVPROPS", SVN_RA_CAPABILITY_COMMIT_REVPROPS },
	{ "SVN_RA_CAPABILITY_ATOMIC_REVPROPS", SVN_RA_CAPABILITY_ATOMIC_REVPROPS },
	{ "SVN_RA_CAPABILITY_INHERITED_PROPS", SVN_RA_CAPABILITY_INHERITED_PROPS },
	{ "SVN_RA_CAPABILITY_EPHEMERAL_TXNPROPS", SVN_RA_CAPABILITY_EPHEMERAL_TXNPROPS },
	{ "SVN_RA_CAPABILITY_GET_FILE_REVS_REVERSE", SVN_RA_CAPABILITY_GET_FILE_REVS_REVERSE },
	{ "SVN_RA_CAPABILITY_LIST", SVN_RA_CAPABILITY_LIST },
	{ NULL, NULL }
};

#undef E

/***************************************************************************/
/*                                                                         */
/* Records.                                                                */
/*                                                                         */
/* Subversion delivers a status, a log entry, a directory listing, and     */
/* the like through a C callback, one item at a time. A CLIPS program      */
/* cannot supply a callback. So a wrapper collects the items into a list   */
/* of records and returns the list as a handle. Every record of a list has */
/* the same fields, named by a schema. The values are CLIPS values, held   */
/* until the list is freed. So the list does not depend on any Subversion  */
/* pool, and it can be read after the context that made it is gone.        */
/*                                                                         */
/* One set of functions serves every kind of list: svn-records-count,      */
/* svn-record-byindex, svn-record-field, svn-record-to-fact, and the       */
/* others. The field names are the slot names that svn-record-to-fact      */
/* fills.                                                                  */
/*                                                                         */
/***************************************************************************/

struct SvnSchema
{
	const char *kind;
	int nfields;
	const char *const *fields;
};

struct SvnRecords
{
	const struct SvnSchema *schema;
	size_t count;
	size_t capacity;
	CLIPSValue *values;           /* count * nfields values, each retained */
	struct SvnRecords *related;   /* a second list that belongs to this one */
	bool borrowed;                /* owned by another list, freed with it */
};

/* The status of a working copy path: svn_client_status_t. */
enum {
	ST_PATH, ST_NODE_STATUS, ST_TEXT_STATUS, ST_PROP_STATUS, ST_KIND, ST_REVISION,
	ST_CHANGED_REV, ST_CHANGED_DATE, ST_CHANGED_TIME, ST_CHANGED_AUTHOR, ST_VERSIONED,
	ST_CONFLICTED, ST_COPIED, ST_SWITCHED, ST_LOCKED, ST_FILE_EXTERNAL, ST_CHANGELIST,
	ST_DEPTH, ST_REPOS_RELPATH, ST_REPOS_ROOT_URL, ST_REPOS_NODE_STATUS,
	ST_REPOS_TEXT_STATUS, ST_REPOS_PROP_STATUS, ST_OOD_CHANGED_REV, ST_OOD_CHANGED_AUTHOR,
	ST_LOCK_OWNER, ST_REPOS_LOCK_OWNER, ST_MOVED_FROM, ST_MOVED_TO, ST_ABS_PATH, ST_COUNT
};
static const char *const StatusFields[ST_COUNT] = {
	"path", "node-status", "text-status", "prop-status", "kind", "revision",
	"changed-rev", "changed-date", "changed-time", "changed-author", "versioned",
	"conflicted", "copied", "switched", "locked", "file-external", "changelist",
	"depth", "repos-relpath", "repos-root-url", "repos-node-status",
	"repos-text-status", "repos-prop-status", "ood-changed-rev", "ood-changed-author",
	"lock-owner", "repos-lock-owner", "moved-from", "moved-to", "abs-path"
};
static const struct SvnSchema StatusSchema = { "status", ST_COUNT, StatusFields };

/* A log entry: svn_log_entry_t. */
enum {
	LG_REVISION, LG_AUTHOR, LG_DATE, LG_TIME, LG_MESSAGE, LG_HAS_CHILDREN,
	LG_CHANGED_PATH_COUNT, LG_COUNT
};
static const char *const LogFields[LG_COUNT] = {
	"revision", "author", "date", "time", "message", "has-children",
	"changed-path-count"
};
static const struct SvnSchema LogSchema = { "log", LG_COUNT, LogFields };

/* A path that a revision changed: svn_log_changed_path2_t. */
enum {
	CP_REVISION, CP_ACTION, CP_PATH, CP_KIND, CP_COPYFROM_PATH, CP_COPYFROM_REV,
	CP_TEXT_MODIFIED, CP_PROPS_MODIFIED, CP_COUNT
};
static const char *const ChangedPathFields[CP_COUNT] = {
	"revision", "action", "path", "kind", "copyfrom-path", "copyfrom-rev",
	"text-modified", "props-modified"
};
static const struct SvnSchema ChangedPathSchema = { "changed-path", CP_COUNT, ChangedPathFields };

/* Information about a path: svn_client_info2_t, with its svn_wc_info_t. */
enum {
	IN_PATH, IN_URL, IN_REVISION, IN_REPOS_ROOT_URL, IN_REPOS_UUID, IN_KIND, IN_SIZE,
	IN_LAST_CHANGED_REV, IN_LAST_CHANGED_DATE, IN_LAST_CHANGED_TIME,
	IN_LAST_CHANGED_AUTHOR, IN_LOCK_OWNER, IN_LOCK_TOKEN, IN_LOCK_COMMENT,
	IN_SCHEDULE, IN_COPYFROM_URL, IN_COPYFROM_REV, IN_DEPTH, IN_WC_ROOT,
	IN_CHANGELIST, IN_CHECKSUM, IN_MOVED_FROM, IN_MOVED_TO, IN_COUNT
};
static const char *const InfoFields[IN_COUNT] = {
	"path", "url", "revision", "repos-root-url", "repos-uuid", "kind", "size",
	"last-changed-rev", "last-changed-date", "last-changed-time",
	"last-changed-author", "lock-owner", "lock-token", "lock-comment",
	"schedule", "copyfrom-url", "copyfrom-rev", "depth", "wc-root",
	"changelist", "checksum", "moved-from", "moved-to"
};
static const struct SvnSchema InfoSchema = { "info", IN_COUNT, InfoFields };

/* A directory entry: svn_dirent_t. */
enum {
	DE_PATH, DE_KIND, DE_SIZE, DE_HAS_PROPS, DE_CREATED_REV, DE_DATE, DE_TIME,
	DE_AUTHOR, DE_LOCK_OWNER, DE_LOCK_TOKEN, DE_LOCK_COMMENT, DE_ABS_PATH, DE_COUNT
};
static const char *const DirentFields[DE_COUNT] = {
	"path", "kind", "size", "has-props", "created-rev", "date", "time",
	"author", "lock-owner", "lock-token", "lock-comment", "abs-path"
};
static const struct SvnSchema DirentSchema = { "dirent", DE_COUNT, DirentFields };

/* One line of a blame. */
enum {
	BL_LINE_NUMBER, BL_REVISION, BL_AUTHOR, BL_DATE, BL_TIME, BL_MERGED_REVISION,
	BL_MERGED_PATH, BL_MERGED_AUTHOR, BL_LINE, BL_LOCAL_CHANGE, BL_COUNT
};
static const char *const BlameFields[BL_COUNT] = {
	"line-number", "revision", "author", "date", "time", "merged-revision",
	"merged-path", "merged-author", "line", "local-change"
};
static const struct SvnSchema BlameSchema = { "blame", BL_COUNT, BlameFields };

/* A property of a path. */
enum { PR_PATH, PR_NAME, PR_VALUE, PR_COUNT };
static const char *const PropFields[PR_COUNT] = { "path", "name", "value" };
static const struct SvnSchema PropSchema = { "prop", PR_COUNT, PropFields };

/* A property of a revision. */
enum { RP_NAME, RP_VALUE, RP_COUNT };
static const char *const RevpropFields[RP_COUNT] = { "name", "value" };
static const struct SvnSchema RevpropSchema = { "revprop", RP_COUNT, RevpropFields };

/* A summary of the difference at one path: svn_client_diff_summarize_t. */
enum { DS_PATH, DS_SUMMARIZE_KIND, DS_PROP_CHANGED, DS_NODE_KIND, DS_COUNT };
static const char *const DiffSummaryFields[DS_COUNT] = {
	"path", "summarize-kind", "prop-changed", "node-kind"
};
static const struct SvnSchema DiffSummarySchema = { "diff-summary", DS_COUNT, DiffSummaryFields };

/* A notification from an operation: svn_wc_notify_t. */
enum {
	NT_ACTION, NT_PATH, NT_KIND, NT_REVISION, NT_CONTENT_STATE, NT_PROP_STATE,
	NT_LOCK_STATE, NT_URL, NT_MIME_TYPE, NT_CHANGELIST, NT_ERROR, NT_OLD_REVISION,
	NT_PROP_NAME, NT_COUNT
};
static const char *const NotifyFields[NT_COUNT] = {
	"action", "path", "kind", "revision", "content-state", "prop-state",
	"lock-state", "url", "mime-type", "changelist", "error", "old-revision",
	"prop-name"
};
static const struct SvnSchema NotifySchema = { "notify", NT_COUNT, NotifyFields };

static struct SvnRecords *SvnRecordsNew(
  const struct SvnSchema *schema)
{
	struct SvnRecords *list = (struct SvnRecords *) calloc(1, sizeof *list);

	if (list == NULL)
	{
		return NULL;
	}
	list->schema = schema;
	return list;
}

/* Adds one record with every field nil, and returns its values. */
static CLIPSValue *SvnRecordsAdd(
  struct SvnRecords *list)
{
	CLIPSValue *row;
	int f;

	if (list->count == list->capacity)
	{
		size_t capacity = list->capacity == 0 ? 16 : list->capacity * 2;
		CLIPSValue *values = (CLIPSValue *) realloc(list->values,
		                                            capacity * (size_t) list->schema->nfields * sizeof *values);
		if (values == NULL)
		{
			return NULL;
		}
		list->values = values;
		list->capacity = capacity;
	}

	row = &list->values[list->count * (size_t) list->schema->nfields];
	for (f = 0; f < list->schema->nfields; f++)
	{
		row[f].lexemeValue = CreateSymbol(SvnEnv, "nil");
		RetainCV(SvnEnv, &row[f]);
	}
	list->count++;

	return row;
}

static CLIPSValue *SvnRecordsRow(
  const struct SvnRecords *list,
  size_t index)
{
	return &list->values[index * (size_t) list->schema->nfields];
}

static void SvnRecordsFree(
  struct SvnRecords *list)
{
	size_t i;

	if (list == NULL)
	{
		return;
	}

	if (list->related != NULL)
	{
		SvnRecordsFree(list->related);
	}

	for (i = 0; i < list->count * (size_t) list->schema->nfields; i++)
	{
		ReleaseCV(SvnEnv, &list->values[i]);
	}
	free(list->values);
	free(list);
}

/* A copy, for svn-client-notifications: the caller gets a list to free
   while the context keeps its own. */
static struct SvnRecords *SvnRecordsCopy(
  const struct SvnRecords *from)
{
	struct SvnRecords *to = SvnRecordsNew(from->schema);
	size_t i;

	if (to == NULL)
	{
		return NULL;
	}

	for (i = 0; i < from->count; i++)
	{
		CLIPSValue *row = SvnRecordsAdd(to);
		const CLIPSValue *src = SvnRecordsRow(from, i);
		int f;

		if (row == NULL)
		{
			SvnRecordsFree(to);
			return NULL;
		}
		for (f = 0; f < from->schema->nfields; f++)
		{
			ReleaseCV(SvnEnv, &row[f]);
			row[f].value = src[f].value;
			RetainCV(SvnEnv, &row[f]);
		}
	}

	return to;
}

/* The setters. Each replaces the nil that the field starts with. */

static void SvnSetValue(
  CLIPSValue *row,
  int field,
  void *value)
{
	ReleaseCV(SvnEnv, &row[field]);
	row[field].value = value;
	RetainCV(SvnEnv, &row[field]);
}

static void SvnSetInteger(CLIPSValue *row, int field, long long value)
{
	SvnSetValue(row, field, CreateInteger(SvnEnv, value));
}

static void SvnSetString(CLIPSValue *row, int field, const char *value)
{
	if (value != NULL)
	{
		SvnSetValue(row, field, CreateString(SvnEnv, value));
	}
}

static void SvnSetSymbol(CLIPSValue *row, int field, const char *value)
{
	if (value != NULL)
	{
		SvnSetValue(row, field, CreateSymbol(SvnEnv, value));
	}
}

static void SvnSetBoolean(CLIPSValue *row, int field, svn_boolean_t value)
{
	SvnSetValue(row, field, CreateBoolean(SvnEnv, value ? true : false));
}

static void SvnSetEnum(CLIPSValue *row, int field, const struct SvnEnumEntry *table, long long value)
{
	const char *name = SvnEnumName(table, value);

	if (name != NULL)
	{
		SvnSetSymbol(row, field, name);
	}
	else
	{
		SvnSetInteger(row, field, value);
	}
}

/* A revision number, or nil for SVN_INVALID_REVNUM. */
static void SvnSetRevision(CLIPSValue *row, int field, svn_revnum_t rev)
{
	if (SVN_IS_VALID_REVNUM(rev))
	{
		SvnSetInteger(row, field, (long long) rev);
	}
}

/* A time, as seconds since the epoch, or nil for zero. */
static void SvnSetTime(CLIPSValue *row, int field, apr_time_t when)
{
	if (when != 0)
	{
		SvnSetInteger(row, field, (long long) apr_time_sec(when));
	}
}

/* The same time as the string that Subversion writes:
   2026-09-05T12:34:56.123456Z. */
static void SvnSetDate(CLIPSValue *row, int field, apr_time_t when, apr_pool_t *pool)
{
	if (when != 0)
	{
		SvnSetString(row, field, svn_time_to_cstring(when, pool));
	}
}

/* A date string as stored in a revision property, both as the string and
   as seconds. */
static void SvnSetDateString(CLIPSValue *row, int dateField, int timeField, const char *date, apr_pool_t *pool)
{
	apr_time_t when;
	svn_error_t *err;

	if (date == NULL)
	{
		return;
	}
	SvnSetString(row, dateField, date);
	err = svn_time_from_cstring(&when, date, pool);
	if (err == SVN_NO_ERROR)
	{
		SvnSetTime(row, timeField, when);
	}
	else
	{
		svn_error_clear(err);
	}
}

static const char *SvnStringValue(const svn_string_t *s)
{
	return s != NULL ? s->data : NULL;
}

static const char *SvnHashString(apr_hash_t *hash, const char *key)
{
	return hash != NULL ? SvnStringValue(svn_hash_gets(hash, key)) : NULL;
}

/***************************************************************************/
/*                                                                         */
/* Handles.                                                                */
/*                                                                         */
/* A context, a repository access session, a repository, and a list of     */
/* records go into CLIPS as external addresses. This file records each one */
/* before it goes out. The record has the kind of the pointer and the      */
/* handle that owns it, if one does. A session belongs to the context that */
/* opened it. The changed paths of a log belong to the log.                */
/*                                                                         */
/* The record gives these results:                                         */
/*                                                                         */
/*   - A handle of the wrong kind is rejected by name.                     */
/*   - A freed handle is not in the table. A second free gives a message,  */
/*     not a crash.                                                        */
/*   - A free of a context first frees the sessions that came from it. A   */
/*     session uses the authentication and configuration of its context,  */
/*     which are in the pool of the context.                               */
/*                                                                         */
/***************************************************************************/

enum SvnKind
{
	SVN_K_CONTEXT,
	SVN_K_SESSION,
	SVN_K_REPOS,
	SVN_K_RECORDS,
	SVN_K_COUNT
};

#define SVN_KM(kind) (1u << (kind))

static const char *SvnKindNames[SVN_K_COUNT] = {
	"context", "ra session", "repository", "records"
};

struct SvnContext
{
	apr_pool_t *pool;
	svn_client_ctx_t *ctx;
	svn_auth_baton_t *auth;
	const char *configDir;
	const char *username;
	const char *password;
	const char *logMessage;      /* the message for the operation in progress */
	struct SvnRecords *notify;   /* the notifications of the last operation */
};

struct SvnSession
{
	apr_pool_t *pool;
	svn_ra_session_t *ra;
	struct SvnContext *owner;
};

struct SvnRepos
{
	apr_pool_t *pool;
	svn_repos_t *repos;
	svn_fs_t *fs;
	const char *path;
};

struct SvnHandle
{
	void *ptr;
	enum SvnKind kind;
	void *parent;
	struct SvnHandle *next;
};

#define SVN_HANDLE_BUCKETS 256

static struct SvnHandle *SvnHandles[SVN_HANDLE_BUCKETS];
static size_t SvnHandleCount = 0;

static unsigned int SvnHandleBucket(
  const void *ptr)
{
	size_t v = (size_t) ptr;
	return (unsigned int) ((v >> 4) ^ (v >> 12) ^ (v >> 20)) % SVN_HANDLE_BUCKETS;
}

static struct SvnHandle *SvnHandleFind(
  const void *ptr)
{
	struct SvnHandle *h;

	for (h = SvnHandles[SvnHandleBucket(ptr)]; h != NULL; h = h->next)
	{
		if (h->ptr == ptr)
		{
			return h;
		}
	}

	return NULL;
}

static struct SvnHandle *SvnHandleRegister(
  void *ptr,
  enum SvnKind kind,
  void *parent)
{
	struct SvnHandle *h = SvnHandleFind(ptr);
	unsigned int bucket;

	if (h != NULL)
	{
		return h;
	}

	h = (struct SvnHandle *) calloc(1, sizeof *h);
	if (h == NULL)
	{
		return NULL;
	}

	h->ptr = ptr;
	h->kind = kind;
	h->parent = parent;

	bucket = SvnHandleBucket(ptr);
	h->next = SvnHandles[bucket];
	SvnHandles[bucket] = h;
	SvnHandleCount++;

	return h;
}

/* CLIPS keeps one external-address value for each pointer. All the
   variables that hold the pointer share that value. This file makes that
   value empty when the handle is freed. A variable that still holds the
   value then reads as freed, not as a pointer that a different object can
   now use. */
static void SvnHandleUnlink(
  struct SvnHandle *dead)
{
	struct SvnHandle **link = &SvnHandles[SvnHandleBucket(dead->ptr)];

	while (*link != NULL)
	{
		if (*link == dead)
		{
			*link = dead->next;
			if (SvnEnv != NULL)
			{
				CreateCExternalAddress(SvnEnv, dead->ptr)->contents = NULL;
			}
			free(dead);
			SvnHandleCount--;
			return;
		}
		link = &(*link)->next;
	}
}

static void SvnFreeByKind(
  enum SvnKind kind,
  void *ptr)
{
	switch (kind)
	{
		case SVN_K_CONTEXT:
		{
			struct SvnContext *c = (struct SvnContext *) ptr;
			SvnRecordsFree(c->notify);
			svn_pool_destroy(c->pool);
			break;
		}
		case SVN_K_SESSION:
		{
			struct SvnSession *s = (struct SvnSession *) ptr;
			svn_pool_destroy(s->pool);
			break;
		}
		case SVN_K_REPOS:
		{
			struct SvnRepos *r = (struct SvnRepos *) ptr;
			svn_pool_destroy(r->pool);
			break;
		}
		case SVN_K_RECORDS:
			SvnRecordsFree((struct SvnRecords *) ptr);
			break;
		case SVN_K_COUNT:
			break;
	}
}

/* Unlinks the handles that belong to a pointer, without a free of their
   own: they go with their owner. */
static void SvnHandleForgetOwned(
  void *parent)
{
	unsigned int b;
	struct SvnHandle *h, *next;

	for (b = 0; b < SVN_HANDLE_BUCKETS; b++)
	{
		for (h = SvnHandles[b]; h != NULL; h = next)
		{
			next = h->next;
			if (h->parent == parent)
			{
				SvnHandleForgetOwned(h->ptr);
				SvnHandleUnlink(h);
			}
		}
	}
}

/* Frees a handle: first what belongs to it, then the object. A session
   that belongs to a context is freed here by its own free as well, because
   its pool is a subpool of nothing but the root. */
static void SvnHandleRelease(
  struct SvnHandle *h)
{
	unsigned int b;
	struct SvnHandle *child, *next;
	bool again = true;

	/* The sessions of a context, and the related lists of a list. A
	   session has a pool of its own, so it is released. A related list
	   is freed by the list that owns it. */
	while (again)
	{
		again = false;
		for (b = 0; b < SVN_HANDLE_BUCKETS && !again; b++)
		{
			for (child = SvnHandles[b]; child != NULL; child = next)
			{
				next = child->next;
				if (child->parent == h->ptr)
				{
					if (child->kind == SVN_K_SESSION)
					{
						SvnHandleRelease(child);
						again = true;
						break;
					}
					SvnHandleForgetOwned(child->ptr);
					SvnHandleUnlink(child);
					again = true;
					break;
				}
			}
		}
	}

	SvnFreeByKind(h->kind, h->ptr);
	SvnHandleUnlink(h);
}

/***************************************************************************/
/*                                                                         */
/* Arguments.                                                              */
/*                                                                         */
/* CLIPS checks literal arguments against the restriction string that      */
/* AddUDF receives. CLIPS does not check a value that comes to a wrapper   */
/* through a variable. So this file checks each argument again. The        */
/* helpers return NULL or false after they write the message and set the   */
/* return value. Then the guard in a wrapper is one line for each          */
/* argument.                                                               */
/*                                                                         */
/***************************************************************************/

static void *SvnHandleArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  unsigned int kinds,
  const char *what,
  struct SvnHandle **handleOut)
{
	UDFValue theArg;
	struct SvnHandle *h;
	char msg[192];

	if (handleOut != NULL)
	{
		*handleOut = NULL;
	}

	if (!UDFNextArgument(context, EXTERNAL_ADDRESS_BIT, &theArg) ||
	    theArg.header->type != EXTERNAL_ADDRESS_TYPE)
	{
		snprintf(msg, sizeof msg, "expected a %s handle", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return NULL;
	}

	if (theArg.externalAddressValue->contents == NULL)
	{
		snprintf(msg, sizeof msg, "the %s handle has been freed", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return NULL;
	}

	h = SvnHandleFind(theArg.externalAddressValue->contents);
	if (h == NULL)
	{
		snprintf(msg, sizeof msg,
		         "the %s handle is not one this library handed out, or has been freed", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return NULL;
	}

	if ((kinds & SVN_KM(h->kind)) == 0)
	{
		snprintf(msg, sizeof msg, "expected a %s handle, got a %s", what, SvnKindNames[h->kind]);
		SvnFail(theEnv, returnValue, fname, msg);
		return NULL;
	}

	if (handleOut != NULL)
	{
		*handleOut = h;
	}
	return h->ptr;
}

#define SvnContextArgument(env, ctx, ret, fname) \
	((struct SvnContext *) SvnHandleArgument(env, ctx, ret, fname, SVN_KM(SVN_K_CONTEXT), "context", NULL))
#define SvnSessionArgument(env, ctx, ret, fname) \
	((struct SvnSession *) SvnHandleArgument(env, ctx, ret, fname, SVN_KM(SVN_K_SESSION), "ra session", NULL))
#define SvnReposArgument(env, ctx, ret, fname) \
	((struct SvnRepos *) SvnHandleArgument(env, ctx, ret, fname, SVN_KM(SVN_K_REPOS), "repository", NULL))
#define SvnRecordsArgument(env, ctx, ret, fname) \
	((struct SvnRecords *) SvnHandleArgument(env, ctx, ret, fname, SVN_KM(SVN_K_RECORDS), "records", NULL))

/* A symbol or a string. A CLIPS program writes the symbol nil for a value
   that is not present. So this helper accepts nil where Subversion accepts
   a NULL pointer. For nil, it returns NULL and writes no message. */
static bool SvnTextArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  bool nilIsNull,
  const char **out)
{
	UDFValue theArg;
	char msg[128];

	if (!UDFNextArgument(context, LEXEME_BITS, &theArg) ||
	    (theArg.header->type != SYMBOL_TYPE && theArg.header->type != STRING_TYPE))
	{
		snprintf(msg, sizeof msg, "expected a symbol or a string for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (nilIsNull &&
	    theArg.header->type == SYMBOL_TYPE &&
	    strcmp(theArg.lexemeValue->contents, "nil") == 0)
	{
		*out = NULL;
		return true;
	}

	*out = theArg.lexemeValue->contents;
	return true;
}

/* An optional text argument. If the argument is not present or is nil, the
   result is NULL. */
static bool SvnOptionalTextArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  const char **out)
{
	*out = NULL;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}
	return SvnTextArgument(theEnv, context, returnValue, fname, what, true, out);
}

static bool SvnIntegerArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  long long *out)
{
	UDFValue theArg;
	char msg[128];

	if (!UDFNextArgument(context, INTEGER_BIT, &theArg) ||
	    theArg.header->type != INTEGER_TYPE)
	{
		snprintf(msg, sizeof msg, "expected an integer for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = theArg.integerValue->contents;
	return true;
}

/* An optional integer. If the argument is not present, the result is the
   given default. */
static bool SvnOptionalIntegerArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  long long defaultValue,
  long long *out)
{
	UDFValue theArg;
	char msg[128];

	*out = defaultValue;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}

	if (!UDFNextArgument(context, INTEGER_BIT | SYMBOL_BIT, &theArg))
	{
		snprintf(msg, sizeof msg, "expected an integer or nil for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}
	if (theArg.header->type == SYMBOL_TYPE)
	{
		if (strcmp(theArg.lexemeValue->contents, "nil") == 0) { return true; }
		snprintf(msg, sizeof msg, "expected an integer or nil for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}
	*out = theArg.integerValue->contents;
	return true;
}

/* An integer that is not negative, for a count or a position. */
static bool SvnSizeArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  size_t *out)
{
	long long value;
	char msg[128];

	if (!SvnIntegerArgument(theEnv, context, returnValue, fname, what, &value))
	{
		return false;
	}

	if (value < 0)
	{
		snprintf(msg, sizeof msg, "%s cannot be negative", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = (size_t) value;
	return true;
}

/* A revision number: an integer that is not negative. */
static bool SvnRevnumArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  svn_revnum_t *out)
{
	long long value;
	char msg[128];

	if (!SvnIntegerArgument(theEnv, context, returnValue, fname, what, &value))
	{
		return false;
	}

	if (value < 0)
	{
		snprintf(msg, sizeof msg, "%s cannot be negative", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = (svn_revnum_t) value;
	return true;
}

/* An optional revision number. If the argument is not present or is nil,
   the result is the given default, usually SVN_INVALID_REVNUM for "the
   youngest". */
static bool SvnOptionalRevnumArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  svn_revnum_t defaultValue,
  svn_revnum_t *out)
{
	UDFValue theArg;
	char msg[128];

	*out = defaultValue;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}

	if (!UDFNextArgument(context, INTEGER_BIT | SYMBOL_BIT, &theArg))
	{
		snprintf(msg, sizeof msg, "expected an integer or nil for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.header->type == SYMBOL_TYPE)
	{
		if (strcmp(theArg.lexemeValue->contents, "nil") == 0 ||
		    strcmp(theArg.lexemeValue->contents, "HEAD") == 0)
		{
			return true;
		}
		snprintf(msg, sizeof msg, "expected an integer or nil for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.integerValue->contents < 0)
	{
		snprintf(msg, sizeof msg, "%s cannot be negative", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = (svn_revnum_t) theArg.integerValue->contents;
	return true;
}

/* An optional boolean. If the argument is not present or is nil, the result
   is the given default. */
static bool SvnOptionalBooleanArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  svn_boolean_t defaultValue,
  svn_boolean_t *out)
{
	UDFValue theArg;
	char msg[128];

	*out = defaultValue;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}

	if (!UDFNextArgument(context, LEXEME_BITS, &theArg) || theArg.header->type != SYMBOL_TYPE)
	{
		snprintf(msg, sizeof msg, "expected TRUE, FALSE or nil for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.lexemeValue == TrueSymbol(theEnv)) { *out = TRUE; return true; }
	if (theArg.lexemeValue == FalseSymbol(theEnv)) { *out = FALSE; return true; }
	if (strcmp(theArg.lexemeValue->contents, "nil") == 0) { return true; }

	snprintf(msg, sizeof msg, "expected TRUE, FALSE or nil for %s", what);
	SvnFail(theEnv, returnValue, fname, msg);
	return false;
}

/* A value from an enumeration table: the symbol with the C name, one of the
   words that the command line uses where the table has them, or the
   integer. */
static bool SvnEnumValue(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  const struct SvnEnumEntry *table,
  const UDFValue *theArg,
  long long *out)
{
	char msg[192];
	int i;

	if (theArg->header->type == INTEGER_TYPE)
	{
		*out = theArg->integerValue->contents;
		return true;
	}

	if (theArg->header->type != SYMBOL_TYPE && theArg->header->type != STRING_TYPE)
	{
		snprintf(msg, sizeof msg, "expected a symbol or an integer for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	for (i = 0; table[i].name != NULL; i++)
	{
		if (strcmp(table[i].name, theArg->lexemeValue->contents) == 0)
		{
			*out = table[i].value;
			return true;
		}
	}

	snprintf(msg, sizeof msg, "%s is not a name this Subversion has for %s",
	         theArg->lexemeValue->contents, what);
	SvnFail(theEnv, returnValue, fname, msg);
	return false;
}

static bool SvnEnumArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  const struct SvnEnumEntry *table,
  long long *out)
{
	UDFValue theArg;
	char msg[192];

	if (!UDFNextArgument(context, INTEGER_BIT | LEXEME_BITS, &theArg))
	{
		snprintf(msg, sizeof msg, "expected a symbol or an integer for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	return SvnEnumValue(theEnv, returnValue, fname, what, table, &theArg, out);
}

/* An optional enumeration. If the argument is not present or is nil, the
   result is the given default. */
static bool SvnOptionalEnumArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  const struct SvnEnumEntry *table,
  long long defaultValue,
  long long *out)
{
	UDFValue theArg;
	char msg[192];

	*out = defaultValue;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}

	if (!UDFNextArgument(context, INTEGER_BIT | LEXEME_BITS, &theArg))
	{
		snprintf(msg, sizeof msg, "expected a symbol or an integer for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.header->type == SYMBOL_TYPE && strcmp(theArg.lexemeValue->contents, "nil") == 0)
	{
		return true;
	}

	return SvnEnumValue(theEnv, returnValue, fname, what, table, &theArg, out);
}

#define SvnDepthArgument(env, ctx, ret, fname, out) \
	SvnEnumArgument(env, ctx, ret, fname, "the depth", SvnDepthNames, out)
#define SvnOptionalDepthArgument(env, ctx, ret, fname, def, out) \
	SvnOptionalEnumArgument(env, ctx, ret, fname, "the depth", SvnDepthNames, def, out)

/* A revision: an integer for a number; one of the symbols HEAD, BASE,
   COMMITTED, PREV and WORKING, or the C name of one of those; nil for
   unspecified; or a string that the command line would accept, such as
   "{2026-01-01}" for a date. */
static bool SvnRevisionArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  apr_pool_t *pool,
  svn_opt_revision_t *out)
{
	UDFValue theArg;
	char msg[192];
	int i;
	svn_opt_revision_t start, end;

	memset(out, 0, sizeof *out);
	out->kind = svn_opt_revision_unspecified;

	if (!UDFNextArgument(context, INTEGER_BIT | LEXEME_BITS, &theArg))
	{
		snprintf(msg, sizeof msg, "expected a revision for %s: an integer, HEAD, BASE, COMMITTED, PREV, WORKING, a date in braces, or nil", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.header->type == INTEGER_TYPE)
	{
		if (theArg.integerValue->contents < 0)
		{
			snprintf(msg, sizeof msg, "%s cannot be negative", what);
			SvnFail(theEnv, returnValue, fname, msg);
			return false;
		}
		out->kind = svn_opt_revision_number;
		out->value.number = (svn_revnum_t) theArg.integerValue->contents;
		return true;
	}

	if (theArg.header->type != SYMBOL_TYPE && theArg.header->type != STRING_TYPE)
	{
		snprintf(msg, sizeof msg, "expected a revision for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (strcmp(theArg.lexemeValue->contents, "nil") == 0)
	{
		return true;
	}

	for (i = 0; SvnRevisionKindNames[i].name != NULL; i++)
	{
		if (strcmp(SvnRevisionKindNames[i].name, theArg.lexemeValue->contents) == 0)
		{
			out->kind = (enum svn_opt_revision_kind) SvnRevisionKindNames[i].value;
			return true;
		}
	}

	start.kind = svn_opt_revision_unspecified;
	end.kind = svn_opt_revision_unspecified;
	if (svn_opt_parse_revision(&start, &end, theArg.lexemeValue->contents, pool) != 0 ||
	    start.kind == svn_opt_revision_unspecified)
	{
		snprintf(msg, sizeof msg, "'%s' is not a revision: expected a number, HEAD, BASE, COMMITTED, PREV, WORKING, or a date in braces",
		         theArg.lexemeValue->contents);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}
	if (end.kind != svn_opt_revision_unspecified)
	{
		snprintf(msg, sizeof msg, "'%s' is a range: %s takes one revision",
		         theArg.lexemeValue->contents, what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = start;
	return true;
}

/* An optional revision. If the argument is not present or is nil, the
   result is the given kind. */
static bool SvnOptionalRevisionArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  apr_pool_t *pool,
  enum svn_opt_revision_kind defaultKind,
  svn_opt_revision_t *out)
{
	memset(out, 0, sizeof *out);
	out->kind = defaultKind;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}
	if (!SvnRevisionArgument(theEnv, context, returnValue, fname, what, pool, out))
	{
		return false;
	}
	if (out->kind == svn_opt_revision_unspecified)
	{
		out->kind = defaultKind;
	}
	return true;
}

/* A path or a URL, in the form that the Subversion client takes: a URL
   canonical, a path absolute and canonical. */
static bool SvnCanonicalTarget(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  const char *text,
  apr_pool_t *pool,
  const char **out)
{
	svn_error_t *err;

	if (svn_path_is_url(text))
	{
		*out = svn_uri_canonicalize(text, pool);
		return true;
	}

	err = svn_dirent_get_absolute(out, svn_dirent_internal_style(text, pool), pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		return false;
	}
	return true;
}

static bool SvnTargetArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  apr_pool_t *pool,
  const char **out)
{
	const char *text;

	if (!SvnTextArgument(theEnv, context, returnValue, fname, what, false, &text))
	{
		return false;
	}
	return SvnCanonicalTarget(theEnv, returnValue, fname, text, pool, out);
}

/* A path that must be a working copy path, not a URL: absolute and
   canonical. */
static bool SvnLocalPathArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  apr_pool_t *pool,
  const char **out)
{
	const char *text;
	svn_error_t *err;
	char msg[192];

	if (!SvnTextArgument(theEnv, context, returnValue, fname, what, false, &text))
	{
		return false;
	}

	if (svn_path_is_url(text))
	{
		snprintf(msg, sizeof msg, "%s must be a working copy path, not a URL", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	err = svn_dirent_get_absolute(out, svn_dirent_internal_style(text, pool), pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		return false;
	}
	return true;
}

/* A URL. */
static bool SvnUrlArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  apr_pool_t *pool,
  const char **out)
{
	const char *text;
	char msg[192];

	if (!SvnTextArgument(theEnv, context, returnValue, fname, what, false, &text))
	{
		return false;
	}

	if (!svn_path_is_url(text))
	{
		snprintf(msg, sizeof msg, "%s must be a URL", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = svn_uri_canonicalize(text, pool);
	return true;
}

/* One target or several: a symbol or a string, or a multifield of them.
   Each is made canonical. The result is an APR array of const char *, and
   single reports if the argument was one value rather than a multifield.
   Then a wrapper that returns one result for each target can return a
   single value for a single target. */
static bool SvnTargetsArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  apr_pool_t *pool,
  apr_array_header_t **out,
  bool *single)
{
	UDFValue theArg;
	char msg[192];
	size_t i;

	if (single != NULL)
	{
		*single = false;
	}

	if (!UDFNextArgument(context, LEXEME_BITS | MULTIFIELD_BIT, &theArg))
	{
		snprintf(msg, sizeof msg, "expected a path, a URL, or a multifield of them for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.header->type == SYMBOL_TYPE || theArg.header->type == STRING_TYPE)
	{
		const char *target;

		if (!SvnCanonicalTarget(theEnv, returnValue, fname, theArg.lexemeValue->contents, pool, &target))
		{
			return false;
		}
		*out = apr_array_make(pool, 1, sizeof(const char *));
		APR_ARRAY_PUSH(*out, const char *) = target;
		if (single != NULL)
		{
			*single = true;
		}
		return true;
	}

	if (theArg.header->type != MULTIFIELD_TYPE)
	{
		snprintf(msg, sizeof msg, "expected a path, a URL, or a multifield of them for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = apr_array_make(pool, (int) theArg.multifieldValue->length, sizeof(const char *));
	for (i = 0; i < theArg.multifieldValue->length; i++)
	{
		CLIPSValue *v = &theArg.multifieldValue->contents[i];
		const char *target;

		if (v->header->type != SYMBOL_TYPE && v->header->type != STRING_TYPE)
		{
			snprintf(msg, sizeof msg, "every element of %s must be a symbol or a string", what);
			SvnFail(theEnv, returnValue, fname, msg);
			return false;
		}
		if (!SvnCanonicalTarget(theEnv, returnValue, fname, v->lexemeValue->contents, pool, &target))
		{
			return false;
		}
		APR_ARRAY_PUSH(*out, const char *) = target;
	}

	if ((*out)->nelts == 0)
	{
		snprintf(msg, sizeof msg, "%s is empty", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	return true;
}

/* A multifield of symbols and strings, or one of them, as an APR array of
   const char *, without canonicalization: repository paths for a log. */
static bool SvnTextsArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  apr_pool_t *pool,
  apr_array_header_t **out)
{
	UDFValue theArg;
	char msg[192];
	size_t i;

	if (!UDFNextArgument(context, LEXEME_BITS | MULTIFIELD_BIT, &theArg))
	{
		snprintf(msg, sizeof msg, "expected a string, a symbol, or a multifield of them for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.header->type == SYMBOL_TYPE || theArg.header->type == STRING_TYPE)
	{
		*out = apr_array_make(pool, 1, sizeof(const char *));
		APR_ARRAY_PUSH(*out, const char *) = theArg.lexemeValue->contents;
		return true;
	}

	if (theArg.header->type != MULTIFIELD_TYPE)
	{
		snprintf(msg, sizeof msg, "expected a string, a symbol, or a multifield of them for %s", what);
		SvnFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = apr_array_make(pool, (int) theArg.multifieldValue->length, sizeof(const char *));
	for (i = 0; i < theArg.multifieldValue->length; i++)
	{
		CLIPSValue *v = &theArg.multifieldValue->contents[i];

		if (v->header->type != SYMBOL_TYPE && v->header->type != STRING_TYPE)
		{
			snprintf(msg, sizeof msg, "every element of %s must be a symbol or a string", what);
			SvnFail(theEnv, returnValue, fname, msg);
			return false;
		}
		APR_ARRAY_PUSH(*out, const char *) = v->lexemeValue->contents;
	}

	return true;
}

/* The name of a deftemplate, as a symbol, resolved. */
static bool SvnDeftemplateArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char **name,
  Deftemplate **out)
{
	UDFValue theArg;

	if (!UDFNextArgument(context, SYMBOL_BIT, &theArg) || theArg.header->type != SYMBOL_TYPE)
	{
		SvnFail(theEnv, returnValue, fname, "expected a symbol for the deftemplate name");
		return false;
	}
	*name = theArg.lexemeValue->contents;
	*out = FindDeftemplate(theEnv, *name);
	if (*out == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "there is no deftemplate by that name");
		return false;
	}
	return true;
}

static bool SvnDefclassArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char **name,
  Defclass **out)
{
	UDFValue theArg;

	if (!UDFNextArgument(context, SYMBOL_BIT, &theArg) || theArg.header->type != SYMBOL_TYPE)
	{
		SvnFail(theEnv, returnValue, fname, "expected a symbol for the defclass name");
		return false;
	}
	*name = theArg.lexemeValue->contents;
	*out = FindDefclass(theEnv, *name);
	if (*out == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "there is no defclass by that name");
		return false;
	}
	return true;
}

/***************************************************************************/
/*                                                                         */
/* Return values.                                                          */
/*                                                                         */
/***************************************************************************/

static void SvnReturnBool(
  Environment *theEnv,
  UDFValue *returnValue,
  svn_boolean_t value)
{
	returnValue->lexemeValue = value ? TrueSymbol(theEnv) : FalseSymbol(theEnv);
}

static void SvnReturnNil(
  Environment *theEnv,
  UDFValue *returnValue)
{
	returnValue->lexemeValue = CreateSymbol(theEnv, "nil");
}

/* A string, or nil for NULL. */
static void SvnReturnText(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *text)
{
	if (text == NULL)
	{
		SvnReturnNil(theEnv, returnValue);
		return;
	}
	returnValue->lexemeValue = CreateString(theEnv, text);
}

static void SvnReturnSymbol(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *text)
{
	if (text == NULL)
	{
		SvnReturnNil(theEnv, returnValue);
		return;
	}
	returnValue->lexemeValue = CreateSymbol(theEnv, text);
}

static void SvnReturnInteger(
  Environment *theEnv,
  UDFValue *returnValue,
  long long value)
{
	returnValue->integerValue = CreateInteger(theEnv, value);
}

/* A revision number, or nil for SVN_INVALID_REVNUM. */
static void SvnReturnRevision(
  Environment *theEnv,
  UDFValue *returnValue,
  svn_revnum_t rev)
{
	if (SVN_IS_VALID_REVNUM(rev))
	{
		SvnReturnInteger(theEnv, returnValue, (long long) rev);
	}
	else
	{
		SvnReturnNil(theEnv, returnValue);
	}
}

static void SvnReturnEnum(
  Environment *theEnv,
  UDFValue *returnValue,
  const struct SvnEnumEntry *table,
  long long value)
{
	const char *name = SvnEnumName(table, value);

	if (name != NULL)
	{
		returnValue->lexemeValue = CreateSymbol(theEnv, name);
	}
	else
	{
		SvnReturnInteger(theEnv, returnValue, value);
	}
}

/* A call that returns an svn_error_t. NULL returns as TRUE. An error
   returns as a rejected call with the Subversion message. */
static bool SvnReturnStatus(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  svn_error_t *err)
{
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		return false;
	}
	returnValue->lexemeValue = TrueSymbol(theEnv);
	return true;
}

/* A new handle. The helper records it and returns it. */
static void SvnReturnHandle(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  void *ptr,
  enum SvnKind kind,
  void *parent)
{
	if (SvnHandleRegister(ptr, kind, parent) == NULL)
	{
		if (parent == NULL)
		{
			SvnFreeByKind(kind, ptr);
		}
		SvnFail(theEnv, returnValue, fname, "out of memory recording the handle");
		return;
	}

	returnValue->externalAddressValue = CreateCExternalAddress(theEnv, ptr);
}

static void SvnReturnRecords(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  struct SvnRecords *list)
{
	if (list == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory collecting the records");
		return;
	}
	SvnReturnHandle(theEnv, returnValue, fname, list, SVN_K_RECORDS, NULL);
}

static int SvnCompareStrings(
  const void *a,
  const void *b)
{
	return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/* The keys of an APR hash, sorted. An APR hash has no order. The keys are
   sorted, so a program sees the same order each time. */
static apr_array_header_t *SvnSortedKeys(
  apr_hash_t *hash,
  apr_pool_t *pool)
{
	apr_array_header_t *keys = apr_array_make(pool, hash != NULL ? (int) apr_hash_count(hash) : 0, sizeof(const char *));
	apr_hash_index_t *hi;

	if (hash == NULL)
	{
		return keys;
	}
	for (hi = apr_hash_first(pool, hash); hi != NULL; hi = apr_hash_next(hi))
	{
		APR_ARRAY_PUSH(keys, const char *) = (const char *) apr_hash_this_key(hi);
	}
	qsort(keys->elts, (size_t) keys->nelts, sizeof(const char *), SvnCompareStrings);
	return keys;
}

/* The properties in an APR hash, as records: one for each name. */
static struct SvnRecords *SvnPropsToRecords(
  apr_hash_t *props,
  const char *path,
  apr_pool_t *pool)
{
	struct SvnRecords *list = SvnRecordsNew(path != NULL ? &PropSchema : &RevpropSchema);
	apr_array_header_t *names;
	int i;

	if (list == NULL || props == NULL)
	{
		return list;
	}

	names = SvnSortedKeys(props, pool);

	for (i = 0; i < names->nelts; i++)
	{
		const char *name = APR_ARRAY_IDX(names, i, const char *);
		const svn_string_t *value = svn_hash_gets(props, name);
		CLIPSValue *row = SvnRecordsAdd(list);

		if (row == NULL)
		{
			SvnRecordsFree(list);
			return NULL;
		}
		if (path != NULL)
		{
			SvnSetString(row, PR_PATH, path);
			SvnSetString(row, PR_NAME, name);
			SvnSetString(row, PR_VALUE, SvnStringValue(value));
		}
		else
		{
			SvnSetString(row, RP_NAME, name);
			SvnSetString(row, RP_VALUE, SvnStringValue(value));
		}
	}

	return list;
}

/***************************************************************************/
/*                                                                         */
/* The library: versions, handles, and errors.                             */
/*                                                                         */
/***************************************************************************/

void SvnVersionFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const svn_version_t *v = svn_client_version();
	char buf[64];

	snprintf(buf, sizeof buf, "%d.%d.%d", v->major, v->minor, v->patch);
	SvnReturnText(theEnv, returnValue, buf);
}

void SvnVersionNumberFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const svn_version_t *v = svn_client_version();

	SvnReturnInteger(theEnv, returnValue, (long long) v->major * 10000 + v->minor * 100 + v->patch);
}

void SvnBuiltVersionFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	SvnReturnInteger(theEnv, returnValue, CLIPSVN_SVN_VERSION_NUM);
}

void SvnVersionTagFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const svn_version_t *v = svn_client_version();

	SvnReturnText(theEnv, returnValue, (v->tag != NULL && v->tag[0] != '\0') ? v->tag : NULL);
}

void SvnRaModulesFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	apr_pool_t *pool = svn_pool_create(SvnRootPool);
	svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);
	svn_error_t *err = svn_ra_print_modules(sb, pool);

	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, "svn-ra-modules", err);
	}
	else
	{
		SvnReturnText(theEnv, returnValue, sb->data);
	}
	svn_pool_destroy(pool);
}

void SvnFsModulesFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	apr_pool_t *pool = svn_pool_create(SvnRootPool);
	svn_stringbuf_t *sb = svn_stringbuf_create_empty(pool);
	svn_error_t *err = svn_fs_print_modules(sb, pool);

	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, "svn-fs-modules", err);
	}
	else
	{
		SvnReturnText(theEnv, returnValue, sb->data);
	}
	svn_pool_destroy(pool);
}

void SvnHandleCountFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	SvnReturnInteger(theEnv, returnValue, (long long) SvnHandleCount);
}

void SvnHandleKindsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, SvnHandleCount);
	unsigned int b;
	struct SvnHandle *h;
	char buf[128];

	for (b = 0; b < SVN_HANDLE_BUCKETS; b++)
	{
		for (h = SvnHandles[b]; h != NULL; h = h->next)
		{
			if (h->parent != NULL)
			{
				struct SvnHandle *owner = SvnHandleFind(h->parent);
				snprintf(buf, sizeof buf, "%s (%s)", SvnKindNames[h->kind],
				         owner != NULL ? SvnKindNames[owner->kind] : "?");
				MBAppendString(mb, buf);
			}
			else
			{
				MBAppendString(mb, SvnKindNames[h->kind]);
			}
		}
	}

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void SvnErrorLastFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	SvnReturnText(theEnv, returnValue, SvnLastMessage[0] != '\0' ? SvnLastMessage : NULL);
}

void SvnErrorLastCodeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	SvnReturnInteger(theEnv, returnValue, (long long) SvnLastCode);
}

void SvnErrorLastNameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	SvnReturnSymbol(theEnv, returnValue, SvnLastName[0] != '\0' ? SvnLastName : NULL);
}

/***************************************************************************/
/*                                                                         */
/* Paths, URLs, times, and words. These take no handle. Each is one call   */
/* into libsvn_subr.                                                       */
/*                                                                         */
/***************************************************************************/

/* A wrapper that takes one text argument and returns one value that a pool
   may be needed for. The argument is in scope as text. */
#define SVN_TEXT_FUNCTION(Cname, fname, what, body) \
void Cname(Environment *theEnv, UDFContext *context, UDFValue *returnValue) \
{ \
	const char *text; \
	apr_pool_t *pool; \
	if (!SvnTextArgument(theEnv, context, returnValue, fname, what, false, &text)) { return; } \
	pool = svn_pool_create(SvnRootPool); \
	body \
	svn_pool_destroy(pool); \
}

SVN_TEXT_FUNCTION(SvnPathIsUrlFunction, "svn-path-is-url", "the text",
	{ SvnReturnBool(theEnv, returnValue, svn_path_is_url(text)); })

SVN_TEXT_FUNCTION(SvnUriCanonicalizeFunction, "svn-uri-canonicalize", "the URL",
	{ SvnReturnText(theEnv, returnValue, svn_uri_canonicalize(text, pool)); })

SVN_TEXT_FUNCTION(SvnDirentCanonicalizeFunction, "svn-dirent-canonicalize", "the path",
	{ SvnReturnText(theEnv, returnValue, svn_dirent_canonicalize(svn_dirent_internal_style(text, pool), pool)); })

SVN_TEXT_FUNCTION(SvnRelpathCanonicalizeFunction, "svn-relpath-canonicalize", "the path",
	{ SvnReturnText(theEnv, returnValue, svn_relpath_canonicalize(text, pool)); })

SVN_TEXT_FUNCTION(SvnDirentGetAbsoluteFunction, "svn-dirent-get-absolute", "the path",
	{
		const char *abs;
		svn_error_t *err = svn_dirent_get_absolute(&abs, svn_dirent_internal_style(text, pool), pool);
		if (err != NULL) { SvnFailError(theEnv, returnValue, "svn-dirent-get-absolute", err); }
		else { SvnReturnText(theEnv, returnValue, abs); }
	})

SVN_TEXT_FUNCTION(SvnDirentIsAbsoluteFunction, "svn-dirent-is-absolute", "the path",
	{ SvnReturnBool(theEnv, returnValue, svn_dirent_is_absolute(svn_dirent_internal_style(text, pool))); })

SVN_TEXT_FUNCTION(SvnUriIsCanonicalFunction, "svn-uri-is-canonical", "the URL",
	{ SvnReturnBool(theEnv, returnValue, svn_uri_is_canonical(text, pool)); })

SVN_TEXT_FUNCTION(SvnDirentIsCanonicalFunction, "svn-dirent-is-canonical", "the path",
	{ SvnReturnBool(theEnv, returnValue, svn_dirent_is_canonical(text, pool)); })

SVN_TEXT_FUNCTION(SvnUriBasenameFunction, "svn-uri-basename", "the URL",
	{ SvnReturnText(theEnv, returnValue, svn_uri_basename(svn_uri_canonicalize(text, pool), pool)); })

SVN_TEXT_FUNCTION(SvnUriDirnameFunction, "svn-uri-dirname", "the URL",
	{ SvnReturnText(theEnv, returnValue, svn_uri_dirname(svn_uri_canonicalize(text, pool), pool)); })

SVN_TEXT_FUNCTION(SvnDirentBasenameFunction, "svn-dirent-basename", "the path",
	{ SvnReturnText(theEnv, returnValue, svn_dirent_basename(svn_dirent_canonicalize(svn_dirent_internal_style(text, pool), pool), pool)); })

SVN_TEXT_FUNCTION(SvnDirentDirnameFunction, "svn-dirent-dirname", "the path",
	{ SvnReturnText(theEnv, returnValue, svn_dirent_dirname(svn_dirent_canonicalize(svn_dirent_internal_style(text, pool), pool), pool)); })

void SvnFileUrlFromDirentFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-file-url-from-dirent";
	const char *text, *abs, *url;
	apr_pool_t *pool;
	svn_error_t *err;

	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the path", false, &text)) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_dirent_get_absolute(&abs, svn_dirent_internal_style(text, pool), pool);
	if (err == NULL) { err = svn_uri_get_file_url_from_dirent(&url, abs, pool); }
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, url); }
	svn_pool_destroy(pool);
}

SVN_TEXT_FUNCTION(SvnDirentFromFileUrlFunction, "svn-dirent-from-file-url", "the URL",
	{
		const char *dirent;
		svn_error_t *err;
		if (!svn_path_is_url(text)) { SvnFail(theEnv, returnValue, "svn-dirent-from-file-url", "expected a file:// URL"); }
		else
		{
			err = svn_uri_get_dirent_from_file_url(&dirent, svn_uri_canonicalize(text, pool), pool);
			if (err != NULL) { SvnFailError(theEnv, returnValue, "svn-dirent-from-file-url", err); }
			else { SvnReturnText(theEnv, returnValue, dirent); }
		}
	})

SVN_TEXT_FUNCTION(SvnTimeFromCstringFunction, "svn-time-from-cstring", "the date",
	{
		apr_time_t when;
		svn_error_t *err = svn_time_from_cstring(&when, text, pool);
		if (err != NULL) { SvnFailError(theEnv, returnValue, "svn-time-from-cstring", err); }
		else { SvnReturnInteger(theEnv, returnValue, (long long) apr_time_sec(when)); }
	})

SVN_TEXT_FUNCTION(SvnDepthFromWordFunction, "svn-depth-from-word", "the word",
	{ SvnReturnEnum(theEnv, returnValue, SvnDepthNames, svn_depth_from_word(text)); })

SVN_TEXT_FUNCTION(SvnNodeKindFromWordFunction, "svn-node-kind-from-word", "the word",
	{ SvnReturnEnum(theEnv, returnValue, SvnNodeKindNames, svn_node_kind_from_word(text)); })

static void SvnAppendRevision(
  MultifieldBuilder *mb,
  const svn_opt_revision_t *rev)
{
	MBAppendSymbol(mb, SvnEnumName(SvnRevisionKindNames, rev->kind));
	if (rev->kind == svn_opt_revision_number) { MBAppendInteger(mb, (long long) rev->value.number); }
	else if (rev->kind == svn_opt_revision_date) { MBAppendInteger(mb, (long long) apr_time_sec(rev->value.date)); }
	else { MBAppendSymbol(mb, "nil"); }
}

void SvnOptParseRevisionFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-opt-parse-revision";
	const char *text;
	svn_opt_revision_t start, end;
	MultifieldBuilder *mb;
	apr_pool_t *pool;

	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the text", false, &text)) { return; }
	pool = svn_pool_create(SvnRootPool);
	start.kind = svn_opt_revision_unspecified;
	end.kind = svn_opt_revision_unspecified;
	if (svn_opt_parse_revision(&start, &end, text, pool) != 0)
	{
		SvnFail(theEnv, returnValue, fname, "the text is not a revision or a revision range");
	}
	else
	{
		mb = CreateMultifieldBuilder(theEnv, 4);
		SvnAppendRevision(mb, &start);
		if (end.kind != svn_opt_revision_unspecified)
		{
			SvnAppendRevision(mb, &end);
		}
		returnValue->multifieldValue = MBCreate(mb);
		MBDispose(mb);
	}
	svn_pool_destroy(pool);
}

void SvnTimeToCstringFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-time-to-cstring";
	long long seconds;
	apr_pool_t *pool;

	if (!SvnIntegerArgument(theEnv, context, returnValue, fname, "the time", &seconds)) { return; }
	pool = svn_pool_create(SvnRootPool);
	SvnReturnText(theEnv, returnValue, svn_time_to_cstring(apr_time_from_sec(seconds), pool));
	svn_pool_destroy(pool);
}

void SvnDepthToWordFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	long long depth;

	if (!SvnDepthArgument(theEnv, context, returnValue, "svn-depth-to-word", &depth)) { return; }
	SvnReturnText(theEnv, returnValue, svn_depth_to_word((svn_depth_t) depth));
}

void SvnNodeKindToWordFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	long long kind;

	if (!SvnEnumArgument(theEnv, context, returnValue, "svn-node-kind-to-word", "the node kind", SvnNodeKindNames, &kind)) { return; }
	SvnReturnText(theEnv, returnValue, svn_node_kind_to_word((svn_node_kind_t) kind));
}

/* Two text arguments, the second joined to or taken from the first. */
#define SVN_TEXT2_FUNCTION(Cname, fname, what1, what2, body) \
void Cname(Environment *theEnv, UDFContext *context, UDFValue *returnValue) \
{ \
	const char *a, *b; \
	apr_pool_t *pool; \
	if (!SvnTextArgument(theEnv, context, returnValue, fname, what1, false, &a)) { return; } \
	if (!SvnTextArgument(theEnv, context, returnValue, fname, what2, false, &b)) { return; } \
	pool = svn_pool_create(SvnRootPool); \
	body \
	svn_pool_destroy(pool); \
}

SVN_TEXT2_FUNCTION(SvnUriJoinFunction, "svn-uri-join", "the URL", "the component",
	{ SvnReturnText(theEnv, returnValue, svn_path_url_add_component2(svn_uri_canonicalize(a, pool), b, pool)); })

SVN_TEXT2_FUNCTION(SvnDirentJoinFunction, "svn-dirent-join", "the path", "the component",
	{ SvnReturnText(theEnv, returnValue, svn_dirent_join(svn_dirent_canonicalize(svn_dirent_internal_style(a, pool), pool), svn_dirent_internal_style(b, pool), pool)); })

SVN_TEXT2_FUNCTION(SvnUriSkipAncestorFunction, "svn-uri-skip-ancestor", "the parent URL", "the child URL",
	{ SvnReturnText(theEnv, returnValue, svn_uri_skip_ancestor(svn_uri_canonicalize(a, pool), svn_uri_canonicalize(b, pool), pool)); })

SVN_TEXT2_FUNCTION(SvnDirentSkipAncestorFunction, "svn-dirent-skip-ancestor", "the parent path", "the child path",
	{ SvnReturnText(theEnv, returnValue, svn_dirent_skip_ancestor(svn_dirent_canonicalize(svn_dirent_internal_style(a, pool), pool), svn_dirent_canonicalize(svn_dirent_internal_style(b, pool), pool))); })

/***************************************************************************/
/*                                                                         */
/* Records: the lists that the collecting functions return.                */
/*                                                                         */
/***************************************************************************/

void SvnRecordsKindFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	struct SvnRecords *list = SvnRecordsArgument(theEnv, context, returnValue, "svn-records-kind");

	if (list == NULL) { return; }
	SvnReturnSymbol(theEnv, returnValue, list->schema->kind);
}

void SvnRecordsCountFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	struct SvnRecords *list = SvnRecordsArgument(theEnv, context, returnValue, "svn-records-count");

	if (list == NULL) { return; }
	SvnReturnInteger(theEnv, returnValue, (long long) list->count);
}

void SvnRecordsFieldsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	struct SvnRecords *list = SvnRecordsArgument(theEnv, context, returnValue, "svn-records-fields");
	MultifieldBuilder *mb;
	int f;

	if (list == NULL) { return; }
	mb = CreateMultifieldBuilder(theEnv, (size_t) list->schema->nfields);
	for (f = 0; f < list->schema->nfields; f++)
	{
		MBAppendSymbol(mb, list->schema->fields[f]);
	}
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

/* The list and an index into it. An index past the end is a refusal, so a
   loop that asks for 0, 1, 2 ... stops on FALSE. That refusal is silent:
   it is the normal end of a loop, not a mistake. */
static bool SvnRecordArguments(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  struct SvnRecords **list,
  size_t *index)
{
	*list = SvnRecordsArgument(theEnv, context, returnValue, fname);
	if (*list == NULL) { return false; }
	if (!SvnSizeArgument(theEnv, context, returnValue, fname, "the index", index)) { return false; }
	if (*index >= (*list)->count)
	{
		returnValue->lexemeValue = FalseSymbol(theEnv);
		return false;
	}
	return true;
}

void SvnRecordByindexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-record-byindex";
	struct SvnRecords *list;
	size_t index;
	CLIPSValue *row;
	MultifieldBuilder *mb;
	int f;

	if (!SvnRecordArguments(theEnv, context, returnValue, fname, &list, &index)) { return; }
	row = SvnRecordsRow(list, index);
	mb = CreateMultifieldBuilder(theEnv, (size_t) list->schema->nfields);
	for (f = 0; f < list->schema->nfields; f++)
	{
		MBAppend(mb, &row[f]);
	}
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void SvnRecordFieldFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-record-field";
	struct SvnRecords *list;
	size_t index;
	CLIPSValue *row;
	const char *name;
	char msg[192];
	int f;

	if (!SvnRecordArguments(theEnv, context, returnValue, fname, &list, &index)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the field name", false, &name)) { return; }
	row = SvnRecordsRow(list, index);
	for (f = 0; f < list->schema->nfields; f++)
	{
		if (strcmp(list->schema->fields[f], name) == 0)
		{
			CLIPSToUDFValue(&row[f], returnValue);
			return;
		}
	}
	snprintf(msg, sizeof msg, "a %s record has no field named %s", list->schema->kind, name);
	SvnFail(theEnv, returnValue, fname, msg);
}

static const char *SvnPutSlotErrorText(
  PutSlotError e)
{
	switch (e)
	{
		case PSE_NO_ERROR:              return "no error";
		case PSE_NULL_POINTER_ERROR:    return "null pointer";
		case PSE_INVALID_TARGET_ERROR:  return "invalid target";
		case PSE_SLOT_NOT_FOUND_ERROR:  return "the slot was not found";
		case PSE_TYPE_ERROR:            return "the value is not of a type the slot allows";
		case PSE_RANGE_ERROR:           return "the value is outside the range the slot allows";
		case PSE_ALLOWED_VALUES_ERROR:  return "the value is not one the slot allows";
		case PSE_CARDINALITY_ERROR:     return "the slot is a multislot";
		case PSE_ALLOWED_CLASSES_ERROR: return "the value is not of a class the slot allows";
		case PSE_EVALUATION_ERROR:      return "evaluation error";
		case PSE_RULE_NETWORK_ERROR:    return "rule network error";
	}
	return "unknown error";
}

static bool SvnIsNil(
  const CLIPSValue *v)
{
	return v->header->type == SYMBOL_TYPE && strcmp(v->lexemeValue->contents, "nil") == 0;
}

/* Puts one value into a slot, as a one-element multifield if the slot is a
   multislot: an empty one for nil. */
static PutSlotError SvnPutSlot(
  Environment *theEnv,
  FactBuilder *fb,
  InstanceBuilder *ib,
  const char *name,
  bool multi,
  CLIPSValue *v)
{
	PutSlotError e;
	CLIPSValue mv;
	MultifieldBuilder *mb;

	if (!multi)
	{
		e = (fb != NULL) ? FBPutSlot(fb, name, v) : IBPutSlot(ib, name, v);
		if (e != PSE_CARDINALITY_ERROR || fb != NULL)
		{
			return e;
		}
		/* An instance slot: there is no cheap test for a multislot, so
		   the cardinality error is the test. */
	}

	mb = CreateMultifieldBuilder(theEnv, 1);
	if (!SvnIsNil(v))
	{
		MBAppend(mb, v);
	}
	mv.multifieldValue = MBCreate(mb);
	MBDispose(mb);
	return (fb != NULL) ? FBPutSlot(fb, name, &mv) : IBPutSlot(ib, name, &mv);
}

/* Fills the slots of a fact builder or an instance builder from one
   record. Only the slots with a field's name are filled. */
static bool SvnRecordFill(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  struct SvnRecords *list,
  size_t index,
  Deftemplate *dt,
  FactBuilder *fb,
  Defclass *dc,
  InstanceBuilder *ib)
{
	CLIPSValue *row = SvnRecordsRow(list, index);
	int f;
	char msg[256];

	for (f = 0; f < list->schema->nfields; f++)
	{
		const char *name = list->schema->fields[f];
		PutSlotError e;

		if (dt != NULL)
		{
			if (!DeftemplateSlotExistP(dt, name)) { continue; }
			e = SvnPutSlot(theEnv, fb, NULL, name, DeftemplateSlotMultiP(dt, name), &row[f]);
		}
		else
		{
			if (!SlotExistP(dc, name, true)) { continue; }
			e = SvnPutSlot(theEnv, NULL, ib, name, false, &row[f]);
		}

		if (e != PSE_NO_ERROR)
		{
			snprintf(msg, sizeof msg, "slot %s could not be written: %s", name, SvnPutSlotErrorText(e));
			SvnFail(theEnv, returnValue, fname, msg);
			return false;
		}
	}

	return true;
}

void SvnRecordToFactFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-record-to-fact";
	struct SvnRecords *list;
	size_t index;
	const char *templateName;
	Deftemplate *dt;
	FactBuilder *fb;
	Fact *fact;

	if (!SvnRecordArguments(theEnv, context, returnValue, fname, &list, &index)) { return; }
	if (!SvnDeftemplateArgument(theEnv, context, returnValue, fname, &templateName, &dt)) { return; }

	fb = CreateFactBuilder(theEnv, templateName);
	if (FBError(theEnv) != FBE_NO_ERROR)
	{
		SvnFail(theEnv, returnValue, fname, "the FactBuilder could not be created");
		FBDispose(fb);
		return;
	}

	if (!SvnRecordFill(theEnv, returnValue, fname, list, index, dt, fb, NULL, NULL))
	{
		FBDispose(fb);
		return;
	}

	/* An identical fact that exists already is what CLIPS answers here, in
	   place of a new one. */
	fact = FBAssert(fb);
	FBDispose(fb);
	if (fact == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "the fact could not be asserted");
		return;
	}
	returnValue->factValue = fact;
}

void SvnRecordsToFactsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-records-to-facts";
	struct SvnRecords *list;
	const char *templateName;
	Deftemplate *dt;
	FactBuilder *fb;
	Fact *fact, *last;
	size_t i;
	long long asserted = 0, highest = 0;

	list = SvnRecordsArgument(theEnv, context, returnValue, fname);
	if (list == NULL) { return; }
	if (!SvnDeftemplateArgument(theEnv, context, returnValue, fname, &templateName, &dt)) { return; }

	/* CLIPS answers an identical fact that exists already in place of a new
	   one. A new fact has an index above every fact that came before it,
	   and that is how the new ones are counted. */
	for (last = GetNextFact(theEnv, NULL); last != NULL; last = GetNextFact(theEnv, last))
	{
		if (FactIndex(last) > highest) { highest = FactIndex(last); }
	}

	fb = CreateFactBuilder(theEnv, templateName);
	if (FBError(theEnv) != FBE_NO_ERROR)
	{
		SvnFail(theEnv, returnValue, fname, "the FactBuilder could not be created");
		FBDispose(fb);
		return;
	}

	for (i = 0; i < list->count; i++)
	{
		if (!SvnRecordFill(theEnv, returnValue, fname, list, i, dt, fb, NULL, NULL))
		{
			FBDispose(fb);
			return;
		}
		fact = FBAssert(fb);
		if (fact != NULL && FactIndex(fact) > highest)
		{
			highest = FactIndex(fact);
			asserted++;
		}
	}

	FBDispose(fb);
	SvnReturnInteger(theEnv, returnValue, asserted);
}

void SvnRecordToInstanceFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-record-to-instance";
	struct SvnRecords *list;
	size_t index;
	const char *className, *instanceName;
	Defclass *dc;
	InstanceBuilder *ib;
	Instance *instance;

	if (!SvnRecordArguments(theEnv, context, returnValue, fname, &list, &index)) { return; }
	if (!SvnDefclassArgument(theEnv, context, returnValue, fname, &className, &dc)) { return; }
	if (!SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the instance name", &instanceName)) { return; }

	ib = CreateInstanceBuilder(theEnv, className);
	if (IBError(theEnv) != IBE_NO_ERROR)
	{
		SvnFail(theEnv, returnValue, fname, "the InstanceBuilder could not be created");
		IBDispose(ib);
		return;
	}

	if (!SvnRecordFill(theEnv, returnValue, fname, list, index, NULL, NULL, dc, ib))
	{
		IBDispose(ib);
		return;
	}

	instance = IBMake(ib, instanceName);
	IBDispose(ib);
	if (instance == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "the instance could not be made");
		return;
	}
	returnValue->instanceValue = instance;
}

void SvnRecordsToInstancesFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-records-to-instances";
	struct SvnRecords *list;
	const char *className;
	Defclass *dc;
	InstanceBuilder *ib;
	size_t i;
	long long made = 0;

	list = SvnRecordsArgument(theEnv, context, returnValue, fname);
	if (list == NULL) { return; }
	if (!SvnDefclassArgument(theEnv, context, returnValue, fname, &className, &dc)) { return; }

	ib = CreateInstanceBuilder(theEnv, className);
	if (IBError(theEnv) != IBE_NO_ERROR)
	{
		SvnFail(theEnv, returnValue, fname, "the InstanceBuilder could not be created");
		IBDispose(ib);
		return;
	}

	for (i = 0; i < list->count; i++)
	{
		if (!SvnRecordFill(theEnv, returnValue, fname, list, i, NULL, NULL, dc, ib))
		{
			IBDispose(ib);
			return;
		}
		if (IBMake(ib, NULL) != NULL)
		{
			made++;
		}
	}

	IBDispose(ib);
	SvnReturnInteger(theEnv, returnValue, made);
}

void SvnRecordsFreeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-records-free";
	struct SvnHandle *h;
	struct SvnRecords *list;

	list = (struct SvnRecords *) SvnHandleArgument(theEnv, context, returnValue, fname, SVN_KM(SVN_K_RECORDS), "records", &h);
	if (list == NULL) { return; }
	if (h->parent != NULL)
	{
		SvnFail(theEnv, returnValue, fname, "these records belong to another list and are freed with it");
		return;
	}
	SvnHandleRelease(h);
	returnValue->lexemeValue = TrueSymbol(theEnv);
}

/* The changed paths that came with a log: a second list, borrowed from the
   first. */
void SvnLogChangedPathsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-log-changed-paths";
	struct SvnRecords *list = SvnRecordsArgument(theEnv, context, returnValue, fname);

	if (list == NULL) { return; }
	if (list->schema != &LogSchema || list->related == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "expected the records of a log");
		return;
	}
	SvnReturnHandle(theEnv, returnValue, fname, list->related, SVN_K_RECORDS, list);
}

/***************************************************************************/
/*                                                                         */
/* Contexts.                                                               */
/*                                                                         */
/* Every client operation takes an svn_client_ctx_t: the authentication,   */
/* the configuration, and the callbacks for notification and for the log   */
/* message of a commit. A context here has a pool of its own, and the      */
/* callbacks write into it: the notifications of the operation in progress */
/* are collected as records, and the log message that the operation was    */
/* given is what the commit gets.                                          */
/*                                                                         */
/***************************************************************************/

static const char *SvnErrorText(
  svn_error_t *err,
  apr_pool_t *pool)
{
	char buf[512];

	if (err == NULL)
	{
		return NULL;
	}
	return apr_pstrdup(pool, svn_err_best_message(err, buf, sizeof buf));
}

static void SvnNotify(
  void *baton,
  const svn_wc_notify_t *n,
  apr_pool_t *pool)
{
	struct SvnContext *c = (struct SvnContext *) baton;
	CLIPSValue *row;

	if (c->notify == NULL)
	{
		return;
	}
	row = SvnRecordsAdd(c->notify);
	if (row == NULL)
	{
		return;
	}

	SvnSetEnum(row, NT_ACTION, SvnNotifyActionNames, n->action);
	SvnSetString(row, NT_PATH, n->path);
	SvnSetEnum(row, NT_KIND, SvnNodeKindNames, n->kind);
	SvnSetRevision(row, NT_REVISION, n->revision);
	SvnSetEnum(row, NT_CONTENT_STATE, SvnNotifyStateNames, n->content_state);
	SvnSetEnum(row, NT_PROP_STATE, SvnNotifyStateNames, n->prop_state);
	SvnSetEnum(row, NT_LOCK_STATE, SvnLockStateNames, n->lock_state);
	SvnSetString(row, NT_URL, n->url);
	SvnSetString(row, NT_MIME_TYPE, n->mime_type);
	SvnSetString(row, NT_CHANGELIST, n->changelist_name);
	SvnSetString(row, NT_ERROR, SvnErrorText(n->err, pool));
	SvnSetRevision(row, NT_OLD_REVISION, n->old_revision);
	SvnSetString(row, NT_PROP_NAME, n->prop_name);
}

static svn_error_t *SvnLogMessage(
  const char **log_msg,
  const char **tmp_file,
  const apr_array_header_t *commit_items,
  void *baton,
  apr_pool_t *pool)
{
	struct SvnContext *c = (struct SvnContext *) baton;

	*log_msg = c->logMessage != NULL ? c->logMessage : "";
	*tmp_file = NULL;
	return SVN_NO_ERROR;
}

/* Records the revision of a commit. */
static svn_error_t *SvnCommitCallback(
  const svn_commit_info_t *info,
  void *baton,
  apr_pool_t *pool)
{
	*(svn_revnum_t *) baton = info->revision;
	return SVN_NO_ERROR;
}

/* The start of an operation: the notifications of the last one go, and
   the log message for this one is set. */
static void SvnContextBegin(
  struct SvnContext *c,
  const char *message)
{
	SvnRecordsFree(c->notify);
	c->notify = SvnRecordsNew(&NotifySchema);
	c->logMessage = message;
}

/* The authentication baton: the providers that answer without a prompt.
   They give the username and password that svn-client-set-auth gave, the
   credentials cached on disk, and the certificate files that the
   configuration names. Nothing here can ask a person a question, so the
   baton is non-interactive, and it writes nothing to the cache.

   A baton remembers the first answer it gave for a realm. So a change of
   username makes a new baton, and the sessions that are open keep the old
   one. */
static void SvnContextOpenAuth(
  struct SvnContext *c)
{
	apr_array_header_t *providers = apr_array_make(c->pool, 6, sizeof(svn_auth_provider_object_t *));
	svn_auth_provider_object_t *provider;

	svn_auth_get_simple_provider2(&provider, NULL, NULL, c->pool);
	APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
	svn_auth_get_username_provider(&provider, c->pool);
	APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
	svn_auth_get_ssl_server_trust_file_provider(&provider, c->pool);
	APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
	svn_auth_get_ssl_client_cert_file_provider(&provider, c->pool);
	APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
	svn_auth_get_ssl_client_cert_pw_file_provider2(&provider, NULL, NULL, c->pool);
	APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
	svn_auth_open(&c->auth, providers, c->pool);
	svn_auth_set_parameter(c->auth, SVN_AUTH_PARAM_NON_INTERACTIVE, "");
	svn_auth_set_parameter(c->auth, SVN_AUTH_PARAM_NO_AUTH_CACHE, "");
	if (c->configDir != NULL)
	{
		svn_auth_set_parameter(c->auth, SVN_AUTH_PARAM_CONFIG_DIR, c->configDir);
	}
	if (c->username != NULL)
	{
		svn_auth_set_parameter(c->auth, SVN_AUTH_PARAM_DEFAULT_USERNAME, c->username);
	}
	if (c->password != NULL)
	{
		svn_auth_set_parameter(c->auth, SVN_AUTH_PARAM_DEFAULT_PASSWORD, c->password);
	}
	if (c->ctx != NULL)
	{
		c->ctx->auth_baton = c->auth;
	}
}

void SvnClientCreateContextFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-create-context";
	const char *configDir;
	apr_pool_t *pool;
	struct SvnContext *c;
	apr_hash_t *cfg;
	svn_error_t *err;

	if (!SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the configuration directory", &configDir)) { return; }

	pool = svn_pool_create(SvnRootPool);
	c = (struct SvnContext *) apr_pcalloc(pool, sizeof *c);
	c->pool = pool;

	if (configDir != NULL)
	{
		err = svn_dirent_get_absolute(&configDir, svn_dirent_internal_style(configDir, pool), pool);
		if (err == NULL)
		{
			err = svn_config_ensure(configDir, pool);
		}
		if (err != NULL)
		{
			SvnFailError(theEnv, returnValue, fname, err);
			svn_pool_destroy(pool);
			return;
		}
	}

	err = svn_config_get_config(&cfg, configDir, pool);
	if (err == NULL)
	{
		err = svn_client_create_context2(&c->ctx, cfg, pool);
	}
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		svn_pool_destroy(pool);
		return;
	}

	c->configDir = configDir;
	SvnContextOpenAuth(c);
	c->ctx->auth_baton = c->auth;
	c->ctx->notify_func2 = SvnNotify;
	c->ctx->notify_baton2 = c;
	c->ctx->log_msg_func3 = SvnLogMessage;
	c->ctx->log_msg_baton3 = c;
	c->ctx->client_name = "CLIPSVN";
	c->notify = SvnRecordsNew(&NotifySchema);

	SvnReturnHandle(theEnv, returnValue, fname, c, SVN_K_CONTEXT, NULL);
}

void SvnClientContextFreeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	struct SvnHandle *h;

	if (SvnHandleArgument(theEnv, context, returnValue, "svn-client-context-free", SVN_KM(SVN_K_CONTEXT), "context", &h) == NULL) { return; }
	SvnHandleRelease(h);
	returnValue->lexemeValue = TrueSymbol(theEnv);
}

void SvnClientSetAuthFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-set-auth";
	struct SvnContext *c;
	const char *username, *password;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the username", true, &username)) { return; }
	if (!SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the password", &password)) { return; }

	c->username = username != NULL ? apr_pstrdup(c->pool, username) : NULL;
	c->password = password != NULL ? apr_pstrdup(c->pool, password) : NULL;
	SvnContextOpenAuth(c);
	returnValue->lexemeValue = TrueSymbol(theEnv);
}

static svn_config_t *SvnContextConfig(
  struct SvnContext *c)
{
	return c->ctx->config != NULL ? svn_hash_gets(c->ctx->config, SVN_CONFIG_CATEGORY_CONFIG) : NULL;
}

void SvnClientSetConfigFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-set-config";
	struct SvnContext *c;
	const char *section, *option, *value;
	svn_config_t *cfg;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the section", false, &section)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the option", false, &option)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the value", true, &value)) { return; }

	cfg = SvnContextConfig(c);
	if (cfg == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "the context has no configuration");
		return;
	}
	svn_config_set(cfg, section, option, value != NULL ? value : "");
	returnValue->lexemeValue = TrueSymbol(theEnv);
}

void SvnClientGetConfigFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-get-config";
	struct SvnContext *c;
	const char *section, *option, *value;
	svn_config_t *cfg;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the section", false, &section)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the option", false, &option)) { return; }

	cfg = SvnContextConfig(c);
	if (cfg == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "the context has no configuration");
		return;
	}
	svn_config_get(cfg, &value, section, option, NULL);
	SvnReturnText(theEnv, returnValue, value);
}

void SvnClientNotificationsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-notifications";
	struct SvnContext *c;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	SvnReturnRecords(theEnv, returnValue, fname, SvnRecordsCopy(c->notify));
}

void SvnClientNotificationCountFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	struct SvnContext *c;

	if ((c = SvnContextArgument(theEnv, context, returnValue, "svn-client-notification-count")) == NULL) { return; }
	SvnReturnInteger(theEnv, returnValue, (long long) (c->notify != NULL ? c->notify->count : 0));
}

/***************************************************************************/
/*                                                                         */
/* Working copies.                                                         */
/*                                                                         */
/***************************************************************************/

/* A revision that is unspecified means head for a URL and the working
   version for a path. The functions that take a revision without a
   documented default get this one. */
static void SvnDefaultRevision(
  svn_opt_revision_t *rev,
  const char *target)
{
	if (rev->kind == svn_opt_revision_unspecified)
	{
		rev->kind = svn_path_is_url(target) ? svn_opt_revision_head : svn_opt_revision_working;
	}
}

/* A committing operation returns the revision that it made, or TRUE if it
   changed only the working copy. */
static void SvnReturnCommitted(
  Environment *theEnv,
  UDFValue *returnValue,
  svn_revnum_t committed)
{
	if (SVN_IS_VALID_REVNUM(committed))
	{
		SvnReturnInteger(theEnv, returnValue, (long long) committed);
	}
	else
	{
		returnValue->lexemeValue = TrueSymbol(theEnv);
	}
}

void SvnClientCheckoutFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-checkout";
	struct SvnContext *c;
	const char *url, *path;
	svn_opt_revision_t revision;
	long long depth;
	svn_boolean_t ignoreExternals;
	svn_revnum_t result;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnUrlArgument(theEnv, context, returnValue, fname, "the URL", pool, &url) ||
	    !SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_head, &revision) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_infinity, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-externals", FALSE, &ignoreExternals))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_checkout3(&result, url, path, &revision, &revision, (svn_depth_t) depth,
	                           ignoreExternals, FALSE, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnInteger(theEnv, returnValue, (long long) result); }
	svn_pool_destroy(pool);
}

void SvnClientUpdateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-update";
	struct SvnContext *c;
	apr_array_header_t *paths, *results;
	bool single;
	svn_opt_revision_t revision;
	long long depth;
	svn_boolean_t depthIsSticky, ignoreExternals;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetsArgument(theEnv, context, returnValue, fname, "the paths", pool, &paths, &single) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_head, &revision) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_unknown, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "depth-is-sticky", FALSE, &depthIsSticky) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-externals", FALSE, &ignoreExternals))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_update4(&results, paths, &revision, (svn_depth_t) depth, depthIsSticky,
	                         ignoreExternals, FALSE, TRUE, FALSE, c->ctx, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
	}
	else if (single)
	{
		SvnReturnRevision(theEnv, returnValue, results->nelts > 0 ? APR_ARRAY_IDX(results, 0, svn_revnum_t) : SVN_INVALID_REVNUM);
	}
	else
	{
		MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, (size_t) results->nelts);
		int i;

		for (i = 0; i < results->nelts; i++)
		{
			MBAppendInteger(mb, (long long) APR_ARRAY_IDX(results, i, svn_revnum_t));
		}
		returnValue->multifieldValue = MBCreate(mb);
		MBDispose(mb);
	}
	svn_pool_destroy(pool);
}

void SvnClientSwitchFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-switch";
	struct SvnContext *c;
	const char *path, *url;
	svn_opt_revision_t revision, peg;
	long long depth;
	svn_boolean_t ignoreExternals;
	svn_revnum_t result;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnUrlArgument(theEnv, context, returnValue, fname, "the URL", pool, &url) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_head, &revision) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_infinity, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-externals", FALSE, &ignoreExternals))
	{
		svn_pool_destroy(pool);
		return;
	}

	peg.kind = svn_opt_revision_unspecified;
	SvnContextBegin(c, NULL);
	err = svn_client_switch3(&result, path, url, &peg, &revision, (svn_depth_t) depth, FALSE,
	                         ignoreExternals, FALSE, TRUE, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnInteger(theEnv, returnValue, (long long) result); }
	svn_pool_destroy(pool);
}

void SvnClientAddFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-add";
	struct SvnContext *c;
	const char *path;
	long long depth;
	svn_boolean_t force, noIgnore, addParents;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_infinity, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", FALSE, &force) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "no-ignore", FALSE, &noIgnore) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "add-parents", FALSE, &addParents))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_client_add5(path, (svn_depth_t) depth, force, noIgnore, FALSE, addParents, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientMkdirFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-mkdir";
	struct SvnContext *c;
	apr_array_header_t *paths;
	svn_boolean_t makeParents;
	const char *message;
	svn_revnum_t committed = SVN_INVALID_REVNUM;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetsArgument(theEnv, context, returnValue, fname, "the paths", pool, &paths, NULL) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "make-parents", FALSE, &makeParents) ||
	    !SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &message))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, message);
	err = svn_client_mkdir4(paths, makeParents, NULL, SvnCommitCallback, &committed, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnCommitted(theEnv, returnValue, committed); }
	svn_pool_destroy(pool);
}

void SvnClientDeleteFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-delete";
	struct SvnContext *c;
	apr_array_header_t *paths;
	svn_boolean_t force, keepLocal;
	const char *message;
	svn_revnum_t committed = SVN_INVALID_REVNUM;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetsArgument(theEnv, context, returnValue, fname, "the paths", pool, &paths, NULL) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", FALSE, &force) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "keep-local", FALSE, &keepLocal) ||
	    !SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &message))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, message);
	err = svn_client_delete4(paths, force, keepLocal, NULL, SvnCommitCallback, &committed, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnCommitted(theEnv, returnValue, committed); }
	svn_pool_destroy(pool);
}

void SvnClientCopyFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-copy";
	struct SvnContext *c;
	const char *source, *destination, *message;
	svn_opt_revision_t revision;
	svn_boolean_t copyAsChild, makeParents;
	svn_client_copy_source_t copySource;
	apr_array_header_t *sources;
	svn_revnum_t committed = SVN_INVALID_REVNUM;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the source", pool, &source) ||
	    !SvnTargetArgument(theEnv, context, returnValue, fname, "the destination", pool, &destination) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the source revision", pool, svn_opt_revision_unspecified, &revision) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "copy-as-child", FALSE, &copyAsChild) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "make-parents", FALSE, &makeParents) ||
	    !SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &message))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnDefaultRevision(&revision, source);
	copySource.path = source;
	copySource.revision = &revision;
	copySource.peg_revision = &revision;
	sources = apr_array_make(pool, 1, sizeof(svn_client_copy_source_t *));
	APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = &copySource;

	SvnContextBegin(c, message);
	err = svn_client_copy7(sources, destination, copyAsChild, makeParents, FALSE, FALSE, FALSE, NULL,
	                       NULL, SvnCommitCallback, &committed, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnCommitted(theEnv, returnValue, committed); }
	svn_pool_destroy(pool);
}

void SvnClientMoveFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-move";
	struct SvnContext *c;
	const char *source, *destination, *message;
	svn_boolean_t moveAsChild, makeParents;
	apr_array_header_t *sources;
	svn_revnum_t committed = SVN_INVALID_REVNUM;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the source", pool, &source) ||
	    !SvnTargetArgument(theEnv, context, returnValue, fname, "the destination", pool, &destination) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "move-as-child", FALSE, &moveAsChild) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "make-parents", FALSE, &makeParents) ||
	    !SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &message))
	{
		svn_pool_destroy(pool);
		return;
	}

	sources = apr_array_make(pool, 1, sizeof(const char *));
	APR_ARRAY_PUSH(sources, const char *) = source;

	SvnContextBegin(c, message);
	err = svn_client_move7(sources, destination, moveAsChild, makeParents, FALSE, FALSE, NULL,
	                       SvnCommitCallback, &committed, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnCommitted(theEnv, returnValue, committed); }
	svn_pool_destroy(pool);
}

void SvnClientCommitFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-commit";
	struct SvnContext *c;
	apr_array_header_t *paths;
	const char *message;
	long long depth;
	svn_boolean_t keepLocks;
	svn_revnum_t committed = SVN_INVALID_REVNUM;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetsArgument(theEnv, context, returnValue, fname, "the paths", pool, &paths, NULL) ||
	    !SvnTextArgument(theEnv, context, returnValue, fname, "the log message", true, &message) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_infinity, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "keep-locks", FALSE, &keepLocks))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, message);
	err = svn_client_commit6(paths, (svn_depth_t) depth, keepLocks, FALSE, TRUE, FALSE, FALSE, NULL, NULL,
	                         SvnCommitCallback, &committed, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRevision(theEnv, returnValue, committed); }
	svn_pool_destroy(pool);
}

void SvnClientRevertFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-revert";
	struct SvnContext *c;
	apr_array_header_t *paths;
	long long depth;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetsArgument(theEnv, context, returnValue, fname, "the paths", pool, &paths, NULL) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_empty, &depth))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_client_revert4(paths, (svn_depth_t) depth, NULL, FALSE, FALSE, FALSE, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientResolveFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-resolve";
	struct SvnContext *c;
	const char *path;
	long long choice, depth;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnEnumArgument(theEnv, context, returnValue, fname, "the choice", SvnConflictChoiceNames, &choice) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_empty, &depth))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_client_resolve(path, (svn_depth_t) depth, (svn_wc_conflict_choice_t) choice, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientCleanupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-cleanup";
	struct SvnContext *c;
	const char *path;
	svn_boolean_t breakLocks, fixTimestamps, vacuumPristines, includeExternals;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "break-locks", TRUE, &breakLocks) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "fix-timestamps", TRUE, &fixTimestamps) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "vacuum-pristines", FALSE, &vacuumPristines) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "include-externals", FALSE, &includeExternals))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_client_cleanup2(path, breakLocks, fixTimestamps, TRUE, vacuumPristines, includeExternals, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientVacuumFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-vacuum";
	struct SvnContext *c;
	const char *path;
	svn_boolean_t removeUnversioned, removeIgnored, fixTimestamps, vacuumPristines, includeExternals;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "remove-unversioned", FALSE, &removeUnversioned) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "remove-ignored", FALSE, &removeIgnored) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "fix-timestamps", FALSE, &fixTimestamps) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "vacuum-pristines", FALSE, &vacuumPristines) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "include-externals", FALSE, &includeExternals))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_client_vacuum(path, removeUnversioned, removeIgnored, fixTimestamps, vacuumPristines, includeExternals, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientRelocateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-relocate";
	struct SvnContext *c;
	const char *path, *from, *to;
	svn_boolean_t ignoreExternals;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnUrlArgument(theEnv, context, returnValue, fname, "the from URL", pool, &from) ||
	    !SvnUrlArgument(theEnv, context, returnValue, fname, "the to URL", pool, &to) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-externals", FALSE, &ignoreExternals))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname, svn_client_relocate2(path, from, to, ignoreExternals, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientUpgradeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-upgrade";
	struct SvnContext *c;
	const char *path;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname, svn_client_upgrade(path, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientLockFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-lock";
	struct SvnContext *c;
	apr_array_header_t *targets;
	const char *comment;
	svn_boolean_t steal;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetsArgument(theEnv, context, returnValue, fname, "the targets", pool, &targets, NULL) ||
	    !SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the comment", &comment) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "steal", FALSE, &steal))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname, svn_client_lock(targets, comment, steal, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientUnlockFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-unlock";
	struct SvnContext *c;
	apr_array_header_t *targets;
	svn_boolean_t breakLock;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetsArgument(theEnv, context, returnValue, fname, "the targets", pool, &targets, NULL) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "break", FALSE, &breakLock))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname, svn_client_unlock(targets, breakLock, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientExportFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-export";
	struct SvnContext *c;
	const char *from, *to;
	svn_opt_revision_t revision, peg;
	svn_boolean_t overwrite, ignoreExternals;
	long long depth;
	svn_revnum_t result;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the source", pool, &from) ||
	    !SvnLocalPathArgument(theEnv, context, returnValue, fname, "the destination", pool, &to) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_unspecified, &revision) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "overwrite", FALSE, &overwrite) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-externals", FALSE, &ignoreExternals) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_infinity, &depth))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnDefaultRevision(&revision, from);
	peg.kind = svn_opt_revision_unspecified;
	SvnContextBegin(c, NULL);
	err = svn_client_export5(&result, from, to, &peg, &revision, overwrite, ignoreExternals, FALSE,
	                         (svn_depth_t) depth, NULL, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRevision(theEnv, returnValue, result); }
	svn_pool_destroy(pool);
}

void SvnClientImportFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-import";
	struct SvnContext *c;
	const char *path, *url, *message;
	long long depth;
	svn_boolean_t noIgnore;
	svn_revnum_t committed = SVN_INVALID_REVNUM;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnUrlArgument(theEnv, context, returnValue, fname, "the URL", pool, &url) ||
	    !SvnTextArgument(theEnv, context, returnValue, fname, "the log message", true, &message) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_infinity, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "no-ignore", FALSE, &noIgnore))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, message);
	err = svn_client_import5(path, url, (svn_depth_t) depth, noIgnore, FALSE, FALSE, NULL, NULL, NULL,
	                         SvnCommitCallback, &committed, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRevision(theEnv, returnValue, committed); }
	svn_pool_destroy(pool);
}

void SvnClientCatFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-cat";
	struct SvnContext *c;
	const char *target;
	svn_opt_revision_t revision, peg;
	svn_boolean_t expandKeywords;
	svn_stringbuf_t *sb;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the target", pool, &target) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_unspecified, &revision) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the peg revision", pool, svn_opt_revision_unspecified, &peg) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "expand-keywords", TRUE, &expandKeywords))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnDefaultRevision(&revision, target);
	sb = svn_stringbuf_create_empty(pool);
	SvnContextBegin(c, NULL);
	err = svn_client_cat3(NULL, svn_stream_from_stringbuf(sb, pool), target, &peg, &revision,
	                      expandKeywords, c->ctx, pool, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, sb->data); }
	svn_pool_destroy(pool);
}

void SvnClientGetWcRootFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-get-wc-root";
	struct SvnContext *c;
	const char *path, *root;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path))
	{
		svn_pool_destroy(pool);
		return;
	}

	err = svn_client_get_wc_root(&root, path, c->ctx, pool, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, root); }
	svn_pool_destroy(pool);
}

void SvnClientGetReposRootFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-get-repos-root";
	struct SvnContext *c;
	const char *target, *root, *uuid;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the target", pool, &target))
	{
		svn_pool_destroy(pool);
		return;
	}

	err = svn_client_get_repos_root(&root, &uuid, target, c->ctx, pool, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
	}
	else
	{
		MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, 2);
		if (root != NULL) { MBAppendString(mb, root); } else { MBAppendSymbol(mb, "nil"); }
		if (uuid != NULL) { MBAppendString(mb, uuid); } else { MBAppendSymbol(mb, "nil"); }
		returnValue->multifieldValue = MBCreate(mb);
		MBDispose(mb);
	}
	svn_pool_destroy(pool);
}

void SvnClientUrlFromPathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-url-from-path";
	struct SvnContext *c;
	const char *target, *url;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the target", pool, &target))
	{
		svn_pool_destroy(pool);
		return;
	}

	err = svn_client_url_from_path2(&url, target, c->ctx, pool, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, url); }
	svn_pool_destroy(pool);
}

void SvnClientMinMaxRevisionsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-min-max-revisions";
	struct SvnContext *c;
	const char *path;
	svn_boolean_t committed;
	svn_revnum_t min, max;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "committed", FALSE, &committed))
	{
		svn_pool_destroy(pool);
		return;
	}

	err = svn_client_min_max_revisions(&min, &max, path, committed, c->ctx, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
	}
	else
	{
		MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, 2);
		MBAppendInteger(mb, (long long) min);
		MBAppendInteger(mb, (long long) max);
		returnValue->multifieldValue = MBCreate(mb);
		MBDispose(mb);
	}
	svn_pool_destroy(pool);
}

/* ---- status ---------------------------------------------------------- */

struct SvnStatusBaton
{
	struct SvnRecords *list;
	const char *target;
};

static svn_error_t *SvnStatusReceiver(
  void *baton,
  const char *path,
  const svn_client_status_t *s,
  apr_pool_t *pool)
{
	struct SvnStatusBaton *b = (struct SvnStatusBaton *) baton;
	CLIPSValue *row = SvnRecordsAdd(b->list);
	const char *rel;

	if (row == NULL)
	{
		return svn_error_create(APR_ENOMEM, NULL, "out of memory collecting the status");
	}

	/* The path is relative to the argument, as the command line prints
	   it. The argument itself is "." if a directory, its name if a file.
	   The absolute path is in abs-path. */
	rel = svn_dirent_skip_ancestor(b->target, s->local_abspath);
	if (rel == NULL)
	{
		rel = s->local_abspath;
	}
	else if (rel[0] == '\0')
	{
		rel = (s->kind == svn_node_dir) ? "." : svn_dirent_basename(s->local_abspath, pool);
	}

	SvnSetString(row, ST_PATH, rel);
	SvnSetEnum(row, ST_NODE_STATUS, SvnStatusKindNames, s->node_status);
	SvnSetEnum(row, ST_TEXT_STATUS, SvnStatusKindNames, s->text_status);
	SvnSetEnum(row, ST_PROP_STATUS, SvnStatusKindNames, s->prop_status);
	SvnSetEnum(row, ST_KIND, SvnNodeKindNames, s->kind);
	SvnSetRevision(row, ST_REVISION, s->revision);
	SvnSetRevision(row, ST_CHANGED_REV, s->changed_rev);
	SvnSetDate(row, ST_CHANGED_DATE, s->changed_date, pool);
	SvnSetTime(row, ST_CHANGED_TIME, s->changed_date);
	SvnSetString(row, ST_CHANGED_AUTHOR, s->changed_author);
	SvnSetBoolean(row, ST_VERSIONED, s->versioned);
	SvnSetBoolean(row, ST_CONFLICTED, s->conflicted);
	SvnSetBoolean(row, ST_COPIED, s->copied);
	SvnSetBoolean(row, ST_SWITCHED, s->switched);
	SvnSetBoolean(row, ST_LOCKED, s->wc_is_locked);
	SvnSetBoolean(row, ST_FILE_EXTERNAL, s->file_external);
	SvnSetString(row, ST_CHANGELIST, s->changelist);
	SvnSetEnum(row, ST_DEPTH, SvnDepthNames, s->depth);
	SvnSetString(row, ST_REPOS_RELPATH, s->repos_relpath);
	SvnSetString(row, ST_REPOS_ROOT_URL, s->repos_root_url);
	SvnSetEnum(row, ST_REPOS_NODE_STATUS, SvnStatusKindNames, s->repos_node_status);
	SvnSetEnum(row, ST_REPOS_TEXT_STATUS, SvnStatusKindNames, s->repos_text_status);
	SvnSetEnum(row, ST_REPOS_PROP_STATUS, SvnStatusKindNames, s->repos_prop_status);
	SvnSetRevision(row, ST_OOD_CHANGED_REV, s->ood_changed_rev);
	SvnSetString(row, ST_OOD_CHANGED_AUTHOR, s->ood_changed_author);
	SvnSetString(row, ST_LOCK_OWNER, s->lock != NULL ? s->lock->owner : NULL);
	SvnSetString(row, ST_REPOS_LOCK_OWNER, s->repos_lock != NULL ? s->repos_lock->owner : NULL);
	SvnSetString(row, ST_MOVED_FROM, s->moved_from_abspath);
	SvnSetString(row, ST_MOVED_TO, s->moved_to_abspath);
	SvnSetString(row, ST_ABS_PATH, s->local_abspath);

	return SVN_NO_ERROR;
}

void SvnClientStatusFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-status";
	struct SvnContext *c;
	const char *path;
	svn_opt_revision_t revision;
	long long depth;
	svn_boolean_t getAll, checkOutOfDate, noIgnore, ignoreExternals;
	struct SvnStatusBaton b;
	svn_revnum_t result;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the path", pool, &path) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_head, &revision) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_infinity, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "get-all", FALSE, &getAll) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "check-out-of-date", FALSE, &checkOutOfDate) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "no-ignore", FALSE, &noIgnore) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-externals", FALSE, &ignoreExternals))
	{
		svn_pool_destroy(pool);
		return;
	}

	b.list = SvnRecordsNew(&StatusSchema);
	b.target = path;
	if (b.list == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory");
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_status6(&result, c->ctx, path, &revision, (svn_depth_t) depth, getAll, checkOutOfDate,
	                         TRUE, noIgnore, ignoreExternals, FALSE, NULL, SvnStatusReceiver, &b, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(b.list);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, b.list);
	}
	svn_pool_destroy(pool);
}

/* ---- log ------------------------------------------------------------- */

/* The receiver that svn-client-log, svn-ra-get-log and svn-repos-get-logs
   share. The log entries go in one list, the changed paths in a second
   that belongs to the first. */
struct SvnLogBaton
{
	struct SvnRecords *log;
	struct SvnRecords *changes;
	size_t pending;   /* changed paths that wait for their revision */
};

static struct SvnRecords *SvnLogRecordsNew(void)
{
	struct SvnRecords *log = SvnRecordsNew(&LogSchema);

	if (log == NULL)
	{
		return NULL;
	}
	log->related = SvnRecordsNew(&ChangedPathSchema);
	if (log->related == NULL)
	{
		SvnRecordsFree(log);
		return NULL;
	}
	log->related->borrowed = true;
	return log;
}

static const char *SvnActionName(
  char action)
{
	switch (action)
	{
		case 'A': return "A";
		case 'D': return "D";
		case 'M': return "M";
		case 'R': return "R";
		default:  return NULL;
	}
}

static svn_error_t *SvnLogEntryRow(
  struct SvnLogBaton *b,
  svn_revnum_t revision,
  apr_hash_t *revprops,
  svn_boolean_t hasChildren,
  long long changed,
  apr_pool_t *pool)
{
	CLIPSValue *row;

	/* Revision 0 has no message and no author: it is the repository
	   before its first commit. The command line leaves it out of a log
	   and so does this, unless a message was put on it. */
	if (revision == 0 && SvnHashString(revprops, SVN_PROP_REVISION_LOG) == NULL)
	{
		return SVN_NO_ERROR;
	}

	row = SvnRecordsAdd(b->log);
	if (row == NULL)
	{
		return svn_error_create(APR_ENOMEM, NULL, "out of memory collecting the log");
	}
	SvnSetInteger(row, LG_REVISION, (long long) revision);
	SvnSetString(row, LG_AUTHOR, SvnHashString(revprops, SVN_PROP_REVISION_AUTHOR));
	SvnSetDateString(row, LG_DATE, LG_TIME, SvnHashString(revprops, SVN_PROP_REVISION_DATE), pool);
	SvnSetString(row, LG_MESSAGE, SvnHashString(revprops, SVN_PROP_REVISION_LOG));
	SvnSetBoolean(row, LG_HAS_CHILDREN, hasChildren);
	SvnSetInteger(row, LG_CHANGED_PATH_COUNT, changed);
	return SVN_NO_ERROR;
}

static svn_error_t *SvnLogReceiver(
  void *baton,
  svn_log_entry_t *e,
  apr_pool_t *pool)
{
	struct SvnLogBaton *b = (struct SvnLogBaton *) baton;
	long long changed = 0;

	/* An entry without a revision closes a group of merged children. */
	if (!SVN_IS_VALID_REVNUM(e->revision))
	{
		return SVN_NO_ERROR;
	}

	if (e->changed_paths2 != NULL)
	{
		apr_array_header_t *paths = SvnSortedKeys(e->changed_paths2, pool);
		int i;

		for (i = 0; i < paths->nelts; i++)
		{
			const char *path = APR_ARRAY_IDX(paths, i, const char *);
			svn_log_changed_path2_t *cp = svn_hash_gets(e->changed_paths2, path);
			CLIPSValue *row = SvnRecordsAdd(b->changes);

			if (row == NULL)
			{
				return svn_error_create(APR_ENOMEM, NULL, "out of memory collecting the log");
			}
			SvnSetInteger(row, CP_REVISION, (long long) e->revision);
			SvnSetSymbol(row, CP_ACTION, SvnActionName(cp->action));
			SvnSetString(row, CP_PATH, path);
			SvnSetEnum(row, CP_KIND, SvnNodeKindNames, cp->node_kind);
			SvnSetString(row, CP_COPYFROM_PATH, cp->copyfrom_path);
			SvnSetRevision(row, CP_COPYFROM_REV, cp->copyfrom_rev);
			SvnSetEnum(row, CP_TEXT_MODIFIED, SvnTristateNames, cp->text_modified);
			SvnSetEnum(row, CP_PROPS_MODIFIED, SvnTristateNames, cp->props_modified);
			changed++;
		}
	}

	return SvnLogEntryRow(b, e->revision, e->revprops, e->has_children, changed, pool);
}

void SvnClientLogFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-log";
	struct SvnContext *c;
	apr_array_header_t *targets, *ranges;
	svn_opt_revision_range_t range;
	svn_opt_revision_t peg;
	long long limit;
	svn_boolean_t discoverChangedPaths, strictNodeHistory;
	struct SvnLogBaton b;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetsArgument(theEnv, context, returnValue, fname, "the targets", pool, &targets, NULL) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the start revision", pool, svn_opt_revision_unspecified, &range.start) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the end revision", pool, svn_opt_revision_unspecified, &range.end) ||
	    !SvnOptionalIntegerArgument(theEnv, context, returnValue, fname, "the limit", 0, &limit) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "discover-changed-paths", TRUE, &discoverChangedPaths) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "strict-node-history", FALSE, &strictNodeHistory) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the peg revision", pool, svn_opt_revision_unspecified, &peg))
	{
		svn_pool_destroy(pool);
		return;
	}
	/* A range that is not given is what the library gives it: HEAD:0 for a
	   URL, BASE:0 for a working copy path, and a start alone is that one
	   revision. */

	ranges = apr_array_make(pool, 1, sizeof(svn_opt_revision_range_t *));
	APR_ARRAY_PUSH(ranges, svn_opt_revision_range_t *) = &range;

	b.log = SvnLogRecordsNew();
	b.changes = b.log != NULL ? b.log->related : NULL;
	b.pending = 0;
	if (b.log == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory");
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_log5(targets, &peg, ranges, (int) limit, discoverChangedPaths, strictNodeHistory,
	                      FALSE, NULL, SvnLogReceiver, &b, c->ctx, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(b.log);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, b.log);
	}
	svn_pool_destroy(pool);
}

/* ---- info ------------------------------------------------------------ */

static svn_error_t *SvnInfoReceiver(
  void *baton,
  const char *path,
  const svn_client_info2_t *info,
  apr_pool_t *pool)
{
	struct SvnRecords *list = (struct SvnRecords *) baton;
	CLIPSValue *row = SvnRecordsAdd(list);

	if (row == NULL)
	{
		return svn_error_create(APR_ENOMEM, NULL, "out of memory collecting the information");
	}

	SvnSetString(row, IN_PATH, path);
	SvnSetString(row, IN_URL, info->URL);
	SvnSetRevision(row, IN_REVISION, info->rev);
	SvnSetString(row, IN_REPOS_ROOT_URL, info->repos_root_URL);
	SvnSetString(row, IN_REPOS_UUID, info->repos_UUID);
	SvnSetEnum(row, IN_KIND, SvnNodeKindNames, info->kind);
	if (info->size != SVN_INVALID_FILESIZE)
	{
		SvnSetInteger(row, IN_SIZE, (long long) info->size);
	}
	SvnSetRevision(row, IN_LAST_CHANGED_REV, info->last_changed_rev);
	SvnSetDate(row, IN_LAST_CHANGED_DATE, info->last_changed_date, pool);
	SvnSetTime(row, IN_LAST_CHANGED_TIME, info->last_changed_date);
	SvnSetString(row, IN_LAST_CHANGED_AUTHOR, info->last_changed_author);
	if (info->lock != NULL)
	{
		SvnSetString(row, IN_LOCK_OWNER, info->lock->owner);
		SvnSetString(row, IN_LOCK_TOKEN, info->lock->token);
		SvnSetString(row, IN_LOCK_COMMENT, info->lock->comment);
	}
	if (info->wc_info != NULL)
	{
		const svn_wc_info_t *wc = info->wc_info;

		SvnSetEnum(row, IN_SCHEDULE, SvnScheduleNames, wc->schedule);
		SvnSetString(row, IN_COPYFROM_URL, wc->copyfrom_url);
		SvnSetRevision(row, IN_COPYFROM_REV, wc->copyfrom_rev);
		SvnSetEnum(row, IN_DEPTH, SvnDepthNames, wc->depth);
		SvnSetString(row, IN_WC_ROOT, wc->wcroot_abspath);
		SvnSetString(row, IN_CHANGELIST, wc->changelist);
		if (wc->checksum != NULL)
		{
			SvnSetString(row, IN_CHECKSUM, svn_checksum_to_cstring(wc->checksum, pool));
		}
		SvnSetString(row, IN_MOVED_FROM, wc->moved_from_abspath);
		SvnSetString(row, IN_MOVED_TO, wc->moved_to_abspath);
	}

	return SVN_NO_ERROR;
}

void SvnClientInfoFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-info";
	struct SvnContext *c;
	const char *target;
	svn_opt_revision_t revision, peg;
	long long depth;
	struct SvnRecords *list;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the target", pool, &target) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_unspecified, &revision) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the peg revision", pool, svn_opt_revision_unspecified, &peg) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_empty, &depth))
	{
		svn_pool_destroy(pool);
		return;
	}

	list = SvnRecordsNew(&InfoSchema);
	if (list == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory");
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_info4(target, &peg, &revision, (svn_depth_t) depth, FALSE, TRUE, FALSE, NULL,
	                       SvnInfoReceiver, list, c->ctx, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(list);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, list);
	}
	svn_pool_destroy(pool);
}

/* ---- list ------------------------------------------------------------ */

static void SvnDirentRow(
  CLIPSValue *row,
  const char *path,
  const svn_dirent_t *dirent,
  const svn_lock_t *lock,
  const char *absPath,
  apr_pool_t *pool)
{
	SvnSetString(row, DE_PATH, path);
	if (dirent != NULL)
	{
		SvnSetEnum(row, DE_KIND, SvnNodeKindNames, dirent->kind);
		if (dirent->size != SVN_INVALID_FILESIZE)
		{
			SvnSetInteger(row, DE_SIZE, (long long) dirent->size);
		}
		SvnSetBoolean(row, DE_HAS_PROPS, dirent->has_props);
		SvnSetRevision(row, DE_CREATED_REV, dirent->created_rev);
		SvnSetDate(row, DE_DATE, dirent->time, pool);
		SvnSetTime(row, DE_TIME, dirent->time);
		SvnSetString(row, DE_AUTHOR, dirent->last_author);
	}
	if (lock != NULL)
	{
		SvnSetString(row, DE_LOCK_OWNER, lock->owner);
		SvnSetString(row, DE_LOCK_TOKEN, lock->token);
		SvnSetString(row, DE_LOCK_COMMENT, lock->comment);
	}
	SvnSetString(row, DE_ABS_PATH, absPath);
}

static svn_error_t *SvnListReceiver(
  void *baton,
  const char *path,
  const svn_dirent_t *dirent,
  const svn_lock_t *lock,
  const char *absPath,
  const char *externalParentUrl,
  const char *externalTarget,
  apr_pool_t *pool)
{
	struct SvnRecords *list = (struct SvnRecords *) baton;
	CLIPSValue *row = SvnRecordsAdd(list);

	if (row == NULL)
	{
		return svn_error_create(APR_ENOMEM, NULL, "out of memory collecting the listing");
	}
	/* The target itself comes as "", which is "." here. */
	SvnDirentRow(row, path[0] != '\0' ? path : ".", dirent, lock, absPath, pool);
	return SVN_NO_ERROR;
}

void SvnClientListFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-list";
	struct SvnContext *c;
	const char *target;
	svn_opt_revision_t revision, peg;
	long long depth;
	svn_boolean_t fetchLocks;
	struct SvnRecords *list;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the target", pool, &target) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_unspecified, &revision) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the peg revision", pool, svn_opt_revision_unspecified, &peg) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_immediates, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "fetch-locks", FALSE, &fetchLocks))
	{
		svn_pool_destroy(pool);
		return;
	}

	/* A listing is of the repository. For a working copy path, the
	   revision it has is the one to list. */
	if (revision.kind == svn_opt_revision_unspecified)
	{
		revision.kind = svn_path_is_url(target) ? svn_opt_revision_head : svn_opt_revision_base;
	}

	list = SvnRecordsNew(&DirentSchema);
	if (list == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory");
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_list4(target, &peg, &revision, NULL, (svn_depth_t) depth, SVN_DIRENT_ALL, fetchLocks,
	                       FALSE, SvnListReceiver, list, c->ctx, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(list);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, list);
	}
	svn_pool_destroy(pool);
}

/* ---- blame ----------------------------------------------------------- */

static svn_error_t *SvnBlameReceiver(
  void *baton,
  apr_int64_t lineNo,
  svn_revnum_t revision,
  apr_hash_t *revProps,
  svn_revnum_t mergedRevision,
  apr_hash_t *mergedRevProps,
  const char *mergedPath,
  const svn_string_t *line,
  svn_boolean_t localChange,
  apr_pool_t *pool)
{
	struct SvnRecords *list = (struct SvnRecords *) baton;
	CLIPSValue *row = SvnRecordsAdd(list);

	if (row == NULL)
	{
		return svn_error_create(APR_ENOMEM, NULL, "out of memory collecting the blame");
	}

	/* Lines are numbered from one here, as the command line numbers them.
	   Subversion counts from zero. */
	SvnSetInteger(row, BL_LINE_NUMBER, (long long) lineNo + 1);
	SvnSetRevision(row, BL_REVISION, revision);
	SvnSetString(row, BL_AUTHOR, SvnHashString(revProps, SVN_PROP_REVISION_AUTHOR));
	SvnSetDateString(row, BL_DATE, BL_TIME, SvnHashString(revProps, SVN_PROP_REVISION_DATE), pool);
	SvnSetRevision(row, BL_MERGED_REVISION, mergedRevision);
	SvnSetString(row, BL_MERGED_PATH, mergedPath);
	SvnSetString(row, BL_MERGED_AUTHOR, SvnHashString(mergedRevProps, SVN_PROP_REVISION_AUTHOR));
	SvnSetString(row, BL_LINE, SvnStringValue(line));
	SvnSetBoolean(row, BL_LOCAL_CHANGE, localChange);
	return SVN_NO_ERROR;
}

void SvnClientBlameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-blame";
	struct SvnContext *c;
	const char *target;
	svn_opt_revision_t start, end, peg;
	svn_revnum_t startRev, endRev;
	struct SvnRecords *list;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the target", pool, &target) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the start revision", pool, svn_opt_revision_number, &start) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the end revision", pool, svn_opt_revision_unspecified, &end) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the peg revision", pool, svn_opt_revision_unspecified, &peg))
	{
		svn_pool_destroy(pool);
		return;
	}
	if (start.kind == svn_opt_revision_number && start.value.number == 0)
	{
		start.value.number = 1;
	}
	if (end.kind == svn_opt_revision_unspecified)
	{
		end.kind = svn_path_is_url(target) ? svn_opt_revision_head : svn_opt_revision_base;
	}

	list = SvnRecordsNew(&BlameSchema);
	if (list == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory");
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_blame6(&startRev, &endRev, target, &peg, &start, &end, svn_diff_file_options_create(pool),
	                        FALSE, FALSE, SvnBlameReceiver, list, c->ctx, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(list);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, list);
	}
	svn_pool_destroy(pool);
}

/* ---- diff ------------------------------------------------------------ */

void SvnClientDiffFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-diff";
	struct SvnContext *c;
	const char *target1, *target2;
	svn_opt_revision_t revision1, revision2;
	long long depth;
	svn_boolean_t ignoreAncestry, gitFormat;
	svn_stringbuf_t *out, *errOut;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the first target", pool, &target1) ||
	    !SvnRevisionArgument(theEnv, context, returnValue, fname, "the first revision", pool, &revision1) ||
	    !SvnTargetArgument(theEnv, context, returnValue, fname, "the second target", pool, &target2) ||
	    !SvnRevisionArgument(theEnv, context, returnValue, fname, "the second revision", pool, &revision2) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_infinity, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-ancestry", FALSE, &ignoreAncestry) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "git-format", FALSE, &gitFormat))
	{
		svn_pool_destroy(pool);
		return;
	}

	out = svn_stringbuf_create_empty(pool);
	errOut = svn_stringbuf_create_empty(pool);
	SvnContextBegin(c, NULL);
	err = svn_client_diff7(apr_array_make(pool, 0, sizeof(const char *)), target1, &revision1, target2, &revision2,
	                       NULL, (svn_depth_t) depth, ignoreAncestry, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
	                       gitFormat, TRUE, "UTF-8", svn_stream_from_stringbuf(out, pool),
	                       svn_stream_from_stringbuf(errOut, pool), NULL, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, out->data); }
	svn_pool_destroy(pool);
}

static svn_error_t *SvnDiffSummarizeReceiver(
  const svn_client_diff_summarize_t *diff,
  void *baton,
  apr_pool_t *pool)
{
	struct SvnRecords *list = (struct SvnRecords *) baton;
	CLIPSValue *row = SvnRecordsAdd(list);

	if (row == NULL)
	{
		return svn_error_create(APR_ENOMEM, NULL, "out of memory collecting the summary");
	}
	SvnSetString(row, DS_PATH, diff->path[0] != '\0' ? diff->path : ".");
	SvnSetEnum(row, DS_SUMMARIZE_KIND, SvnSummarizeKindNames, diff->summarize_kind);
	SvnSetBoolean(row, DS_PROP_CHANGED, diff->prop_changed);
	SvnSetEnum(row, DS_NODE_KIND, SvnNodeKindNames, diff->node_kind);
	return SVN_NO_ERROR;
}

void SvnClientDiffSummarizeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-diff-summarize";
	struct SvnContext *c;
	const char *target1, *target2;
	svn_opt_revision_t revision1, revision2;
	long long depth;
	svn_boolean_t ignoreAncestry;
	struct SvnRecords *list;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the first target", pool, &target1) ||
	    !SvnRevisionArgument(theEnv, context, returnValue, fname, "the first revision", pool, &revision1) ||
	    !SvnTargetArgument(theEnv, context, returnValue, fname, "the second target", pool, &target2) ||
	    !SvnRevisionArgument(theEnv, context, returnValue, fname, "the second revision", pool, &revision2) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_infinity, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-ancestry", FALSE, &ignoreAncestry))
	{
		svn_pool_destroy(pool);
		return;
	}

	list = SvnRecordsNew(&DiffSummarySchema);
	if (list == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory");
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_diff_summarize2(target1, &revision1, target2, &revision2, (svn_depth_t) depth, ignoreAncestry,
	                                 NULL, SvnDiffSummarizeReceiver, list, c->ctx, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(list);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, list);
	}
	svn_pool_destroy(pool);
}

/* ---- merge ----------------------------------------------------------- */

void SvnClientMergeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-merge";
	struct SvnContext *c;
	const char *source1, *source2, *target;
	svn_opt_revision_t revision1, revision2;
	long long depth;
	svn_boolean_t ignoreAncestry, forceDelete, recordOnly, dryRun;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the first source", pool, &source1) ||
	    !SvnRevisionArgument(theEnv, context, returnValue, fname, "the first revision", pool, &revision1) ||
	    !SvnTargetArgument(theEnv, context, returnValue, fname, "the second source", pool, &source2) ||
	    !SvnRevisionArgument(theEnv, context, returnValue, fname, "the second revision", pool, &revision2) ||
	    !SvnLocalPathArgument(theEnv, context, returnValue, fname, "the target", pool, &target) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_unknown, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-ancestry", FALSE, &ignoreAncestry) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "force-delete", FALSE, &forceDelete) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "record-only", FALSE, &recordOnly) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "dry-run", FALSE, &dryRun))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_client_merge5(source1, &revision1, source2, &revision2, target, (svn_depth_t) depth,
	                                  ignoreAncestry, ignoreAncestry, forceDelete, recordOnly, dryRun, FALSE,
	                                  NULL, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientMergePegFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-merge-peg";
	struct SvnContext *c;
	const char *source, *target;
	svn_opt_revision_range_t range;
	svn_opt_revision_t peg;
	apr_array_header_t *ranges;
	long long depth;
	svn_boolean_t ignoreAncestry, forceDelete, recordOnly, dryRun;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the source", pool, &source) ||
	    !SvnLocalPathArgument(theEnv, context, returnValue, fname, "the target", pool, &target) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the start revision", pool, svn_opt_revision_unspecified, &range.start) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the end revision", pool, svn_opt_revision_unspecified, &range.end) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the peg revision", pool, svn_opt_revision_unspecified, &peg) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_unknown, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "ignore-ancestry", FALSE, &ignoreAncestry) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "force-delete", FALSE, &forceDelete) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "record-only", FALSE, &recordOnly) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "dry-run", FALSE, &dryRun))
	{
		svn_pool_destroy(pool);
		return;
	}

	/* No range means the automatic merge: everything eligible. */
	ranges = apr_array_make(pool, 1, sizeof(svn_opt_revision_range_t *));
	if (range.start.kind != svn_opt_revision_unspecified || range.end.kind != svn_opt_revision_unspecified)
	{
		if (range.start.kind == svn_opt_revision_unspecified || range.end.kind == svn_opt_revision_unspecified)
		{
			SvnFail(theEnv, returnValue, fname, "a range needs both a start and an end revision");
			svn_pool_destroy(pool);
			return;
		}
		APR_ARRAY_PUSH(ranges, svn_opt_revision_range_t *) = &range;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_client_merge_peg5(source, ranges, &peg, target, (svn_depth_t) depth, ignoreAncestry,
	                                      ignoreAncestry, forceDelete, recordOnly, dryRun, FALSE, NULL, c->ctx, pool));
	svn_pool_destroy(pool);
}

/* ---- properties ------------------------------------------------------ */

void SvnClientPropgetFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-propget";
	struct SvnContext *c;
	const char *name, *target;
	svn_opt_revision_t revision, peg;
	apr_hash_t *props;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the property name", false, &name) ||
	    !SvnTargetArgument(theEnv, context, returnValue, fname, "the target", pool, &target) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_unspecified, &revision) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the peg revision", pool, svn_opt_revision_unspecified, &peg))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_propget5(&props, NULL, name, target, &peg, &revision, NULL, svn_depth_empty, NULL,
	                          c->ctx, pool, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
	}
	else
	{
		/* One target at depth empty: one entry at most, whatever key
		   Subversion filed it under. */
		apr_hash_index_t *hi = apr_hash_first(pool, props);
		SvnReturnText(theEnv, returnValue, hi != NULL ? SvnStringValue(apr_hash_this_val(hi)) : NULL);
	}
	svn_pool_destroy(pool);
}

static svn_error_t *SvnProplistReceiver(
  void *baton,
  const char *path,
  apr_hash_t *props,
  apr_array_header_t *inheritedProps,
  apr_pool_t *pool)
{
	struct SvnRecords *list = (struct SvnRecords *) baton;
	apr_array_header_t *names = SvnSortedKeys(props, pool);
	int i;

	for (i = 0; i < names->nelts; i++)
	{
		const char *name = APR_ARRAY_IDX(names, i, const char *);
		CLIPSValue *row = SvnRecordsAdd(list);

		if (row == NULL)
		{
			return svn_error_create(APR_ENOMEM, NULL, "out of memory collecting the properties");
		}
		SvnSetString(row, PR_PATH, path);
		SvnSetString(row, PR_NAME, name);
		SvnSetString(row, PR_VALUE, SvnHashString(props, name));
	}
	return SVN_NO_ERROR;
}

void SvnClientProplistFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-proplist";
	struct SvnContext *c;
	const char *target;
	svn_opt_revision_t revision, peg;
	long long depth;
	struct SvnRecords *list;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTargetArgument(theEnv, context, returnValue, fname, "the target", pool, &target) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, svn_opt_revision_unspecified, &revision) ||
	    !SvnOptionalRevisionArgument(theEnv, context, returnValue, fname, "the peg revision", pool, svn_opt_revision_unspecified, &peg) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_empty, &depth))
	{
		svn_pool_destroy(pool);
		return;
	}

	list = SvnRecordsNew(&PropSchema);
	if (list == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory");
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_proplist4(target, &peg, &revision, (svn_depth_t) depth, NULL, FALSE,
	                           SvnProplistReceiver, list, c->ctx, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(list);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, list);
	}
	svn_pool_destroy(pool);
}

void SvnClientPropsetLocalFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-propset-local";
	struct SvnContext *c;
	const char *name, *value;
	apr_array_header_t *targets;
	long long depth;
	svn_boolean_t skipChecks;
	apr_pool_t *pool;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the property name", false, &name) ||
	    !SvnTextArgument(theEnv, context, returnValue, fname, "the value", true, &value) ||
	    !SvnTargetsArgument(theEnv, context, returnValue, fname, "the targets", pool, &targets, NULL) ||
	    !SvnOptionalDepthArgument(theEnv, context, returnValue, fname, svn_depth_empty, &depth) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "skip-checks", FALSE, &skipChecks))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_client_propset_local(name, value != NULL ? svn_string_create(value, pool) : NULL, targets,
	                                         (svn_depth_t) depth, skipChecks, NULL, c->ctx, pool));
	svn_pool_destroy(pool);
}

void SvnClientPropsetRemoteFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-propset-remote";
	struct SvnContext *c;
	const char *name, *value, *url, *message;
	svn_revnum_t baseRevision, committed = SVN_INVALID_REVNUM;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the property name", false, &name) ||
	    !SvnTextArgument(theEnv, context, returnValue, fname, "the value", true, &value) ||
	    !SvnUrlArgument(theEnv, context, returnValue, fname, "the URL", pool, &url) ||
	    !SvnRevnumArgument(theEnv, context, returnValue, fname, "the base revision", &baseRevision) ||
	    !SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &message))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, message);
	err = svn_client_propset_remote(name, value != NULL ? svn_string_create(value, pool) : NULL, url, FALSE,
	                                baseRevision, NULL, SvnCommitCallback, &committed, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRevision(theEnv, returnValue, committed); }
	svn_pool_destroy(pool);
}

void SvnClientRevpropGetFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-revprop-get";
	struct SvnContext *c;
	const char *name, *url;
	svn_opt_revision_t revision;
	svn_string_t *value;
	svn_revnum_t setRev;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the property name", false, &name) ||
	    !SvnUrlArgument(theEnv, context, returnValue, fname, "the URL", pool, &url) ||
	    !SvnRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, &revision))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_revprop_get(name, &value, url, &revision, &setRev, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, SvnStringValue(value)); }
	svn_pool_destroy(pool);
}

void SvnClientRevpropSetFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-revprop-set";
	struct SvnContext *c;
	const char *name, *value, *url;
	svn_opt_revision_t revision;
	svn_boolean_t force;
	svn_revnum_t setRev;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the property name", false, &name) ||
	    !SvnTextArgument(theEnv, context, returnValue, fname, "the value", true, &value) ||
	    !SvnUrlArgument(theEnv, context, returnValue, fname, "the URL", pool, &url) ||
	    !SvnRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, &revision) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", FALSE, &force))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_revprop_set2(name, value != NULL ? svn_string_create(value, pool) : NULL, NULL, url,
	                              &revision, &setRev, force, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRevision(theEnv, returnValue, setRev); }
	svn_pool_destroy(pool);
}

void SvnClientRevpropListFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-revprop-list";
	struct SvnContext *c;
	const char *url;
	svn_opt_revision_t revision;
	apr_hash_t *props;
	svn_revnum_t setRev;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnUrlArgument(theEnv, context, returnValue, fname, "the URL", pool, &url) ||
	    !SvnRevisionArgument(theEnv, context, returnValue, fname, "the revision", pool, &revision))
	{
		svn_pool_destroy(pool);
		return;
	}

	SvnContextBegin(c, NULL);
	err = svn_client_revprop_list(&props, url, &revision, &setRev, c->ctx, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRecords(theEnv, returnValue, fname, SvnPropsToRecords(props, NULL, pool)); }
	svn_pool_destroy(pool);
}

void SvnClientOpenRaSessionFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-client-open-ra-session";
	struct SvnContext *c;
	const char *url;
	struct SvnSession *s;
	apr_pool_t *pool, *scratch;
	svn_error_t *err;

	if ((c = SvnContextArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	scratch = svn_pool_create(SvnRootPool);
	if (!SvnUrlArgument(theEnv, context, returnValue, fname, "the URL", scratch, &url))
	{
		svn_pool_destroy(scratch);
		return;
	}

	pool = svn_pool_create(SvnRootPool);
	s = (struct SvnSession *) apr_pcalloc(pool, sizeof *s);
	s->pool = pool;
	s->owner = c;
	SvnContextBegin(c, NULL);
	err = svn_client_open_ra_session2(&s->ra, url, NULL, c->ctx, pool, scratch);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		svn_pool_destroy(pool);
	}
	else
	{
		SvnReturnHandle(theEnv, returnValue, fname, s, SVN_K_SESSION, c);
	}
	svn_pool_destroy(scratch);
}

/***************************************************************************/
/*                                                                         */
/* Repository access sessions.                                             */
/*                                                                         */
/* A session reads a repository by URL without a working copy. The path    */
/* arguments are relative to the URL of the session: "" or "." for the     */
/* directory the session is opened on, "trunk/README" for a file under it. */
/*                                                                         */
/***************************************************************************/

/* A path relative to the session URL. "." and "/" both mean the session
   URL itself. */
static bool SvnSessionPathArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  apr_pool_t *pool,
  const char **out)
{
	const char *text;

	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the path", true, &text))
	{
		return false;
	}
	if (text == NULL || strcmp(text, ".") == 0)
	{
		*out = "";
		return true;
	}
	*out = svn_relpath_canonicalize(text, pool);
	return true;
}

void SvnRaSessionFreeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	struct SvnHandle *h;

	if (SvnHandleArgument(theEnv, context, returnValue, "svn-ra-session-free", SVN_KM(SVN_K_SESSION), "ra session", &h) == NULL) { return; }
	SvnHandleRelease(h);
	returnValue->lexemeValue = TrueSymbol(theEnv);
}

void SvnRaGetLatestRevnumFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-get-latest-revnum";
	struct SvnSession *s;
	svn_revnum_t rev;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_ra_get_latest_revnum(s->ra, &rev, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnInteger(theEnv, returnValue, (long long) rev); }
	svn_pool_destroy(pool);
}

void SvnRaGetUuidFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-get-uuid";
	struct SvnSession *s;
	const char *uuid;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_ra_get_uuid2(s->ra, &uuid, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, uuid); }
	svn_pool_destroy(pool);
}

void SvnRaGetReposRootFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-get-repos-root";
	struct SvnSession *s;
	const char *url;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_ra_get_repos_root2(s->ra, &url, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, url); }
	svn_pool_destroy(pool);
}

void SvnRaGetSessionUrlFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-get-session-url";
	struct SvnSession *s;
	const char *url;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_ra_get_session_url(s->ra, &url, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, url); }
	svn_pool_destroy(pool);
}

void SvnRaReparentFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-reparent";
	struct SvnSession *s;
	const char *url;
	apr_pool_t *pool;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnUrlArgument(theEnv, context, returnValue, fname, "the URL", pool, &url))
	{
		svn_pool_destroy(pool);
		return;
	}
	SvnReturnStatus(theEnv, returnValue, fname, svn_ra_reparent(s->ra, url, pool));
	svn_pool_destroy(pool);
}

void SvnRaCheckPathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-check-path";
	struct SvnSession *s;
	const char *path;
	svn_revnum_t rev;
	svn_node_kind_t kind;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnSessionPathArgument(theEnv, context, returnValue, fname, pool, &path) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the revision", SVN_INVALID_REVNUM, &rev))
	{
		svn_pool_destroy(pool);
		return;
	}
	err = svn_ra_check_path(s->ra, path, rev, &kind, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnEnum(theEnv, returnValue, SvnNodeKindNames, kind); }
	svn_pool_destroy(pool);
}

void SvnRaStatFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-stat";
	struct SvnSession *s;
	const char *path;
	svn_revnum_t rev;
	svn_dirent_t *dirent;
	struct SvnRecords *list;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnSessionPathArgument(theEnv, context, returnValue, fname, pool, &path) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the revision", SVN_INVALID_REVNUM, &rev))
	{
		svn_pool_destroy(pool);
		return;
	}
	err = svn_ra_stat(s->ra, path, rev, &dirent, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
	}
	else
	{
		list = SvnRecordsNew(&DirentSchema);
		if (list != NULL && dirent != NULL)
		{
			CLIPSValue *row = SvnRecordsAdd(list);
			if (row != NULL)
			{
				SvnDirentRow(row, path[0] != '\0' ? path : ".", dirent, NULL, NULL, pool);
			}
		}
		SvnReturnRecords(theEnv, returnValue, fname, list);
	}
	svn_pool_destroy(pool);
}

void SvnRaGetFileFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-get-file";
	struct SvnSession *s;
	const char *path;
	svn_revnum_t rev;
	svn_stringbuf_t *sb;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnSessionPathArgument(theEnv, context, returnValue, fname, pool, &path) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the revision", SVN_INVALID_REVNUM, &rev))
	{
		svn_pool_destroy(pool);
		return;
	}
	sb = svn_stringbuf_create_empty(pool);
	err = svn_ra_get_file(s->ra, path, rev, svn_stream_from_stringbuf(sb, pool), NULL, NULL, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, sb->data); }
	svn_pool_destroy(pool);
}

void SvnRaGetFilePropsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-get-file-props";
	struct SvnSession *s;
	const char *path;
	svn_revnum_t rev;
	apr_hash_t *props;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnSessionPathArgument(theEnv, context, returnValue, fname, pool, &path) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the revision", SVN_INVALID_REVNUM, &rev))
	{
		svn_pool_destroy(pool);
		return;
	}
	err = svn_ra_get_file(s->ra, path, rev, NULL, NULL, &props, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRecords(theEnv, returnValue, fname, SvnPropsToRecords(props, path[0] != '\0' ? path : ".", pool)); }
	svn_pool_destroy(pool);
}

/* A hash of svn_dirent_t by name, as dirent records, in name order. */
static struct SvnRecords *SvnDirentsToRecords(
  apr_hash_t *dirents,
  const char *parent,
  apr_pool_t *pool)
{
	struct SvnRecords *list = SvnRecordsNew(&DirentSchema);
	apr_array_header_t *names = SvnSortedKeys(dirents, pool);
	int i;

	if (list == NULL)
	{
		return NULL;
	}
	for (i = 0; i < names->nelts; i++)
	{
		const char *name = APR_ARRAY_IDX(names, i, const char *);
		CLIPSValue *row = SvnRecordsAdd(list);

		if (row == NULL)
		{
			SvnRecordsFree(list);
			return NULL;
		}
		SvnDirentRow(row, (parent != NULL && parent[0] != '\0') ? svn_relpath_join(parent, name, pool) : name,
		             svn_hash_gets(dirents, name), NULL, NULL, pool);
	}
	return list;
}

void SvnRaGetDirFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-get-dir";
	struct SvnSession *s;
	const char *path;
	svn_revnum_t rev;
	apr_hash_t *dirents;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnSessionPathArgument(theEnv, context, returnValue, fname, pool, &path) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the revision", SVN_INVALID_REVNUM, &rev))
	{
		svn_pool_destroy(pool);
		return;
	}
	err = svn_ra_get_dir2(s->ra, &dirents, NULL, NULL, path, rev, SVN_DIRENT_ALL, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRecords(theEnv, returnValue, fname, SvnDirentsToRecords(dirents, path, pool)); }
	svn_pool_destroy(pool);
}

void SvnRaGetLogFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-get-log";
	struct SvnSession *s;
	apr_array_header_t *paths;
	svn_revnum_t start, end;
	long long limit;
	svn_boolean_t discoverChangedPaths, strictNodeHistory;
	struct SvnLogBaton b;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTextsArgument(theEnv, context, returnValue, fname, "the paths", pool, &paths) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the start revision", SVN_INVALID_REVNUM, &start) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the end revision", 0, &end) ||
	    !SvnOptionalIntegerArgument(theEnv, context, returnValue, fname, "the limit", 0, &limit) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "discover-changed-paths", TRUE, &discoverChangedPaths) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "strict-node-history", FALSE, &strictNodeHistory))
	{
		svn_pool_destroy(pool);
		return;
	}

	b.log = SvnLogRecordsNew();
	b.changes = b.log != NULL ? b.log->related : NULL;
	b.pending = 0;
	if (b.log == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory");
		svn_pool_destroy(pool);
		return;
	}

	err = svn_ra_get_log2(s->ra, paths, start, end, (int) limit, discoverChangedPaths, strictNodeHistory,
	                      FALSE, NULL, SvnLogReceiver, &b, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(b.log);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, b.log);
	}
	svn_pool_destroy(pool);
}

void SvnRaGetDatedRevisionFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-get-dated-revision";
	struct SvnSession *s;
	const char *date;
	apr_time_t when;
	svn_revnum_t rev;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the date", false, &date)) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_time_from_cstring(&when, date, pool);
	if (err == NULL)
	{
		err = svn_ra_get_dated_revision(s->ra, &rev, when, pool);
	}
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnInteger(theEnv, returnValue, (long long) rev); }
	svn_pool_destroy(pool);
}

void SvnRaRevPropFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-rev-prop";
	struct SvnSession *s;
	svn_revnum_t rev;
	const char *name;
	svn_string_t *value;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnRevnumArgument(theEnv, context, returnValue, fname, "the revision", &rev)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the property name", false, &name)) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_ra_rev_prop(s->ra, rev, name, &value, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, SvnStringValue(value)); }
	svn_pool_destroy(pool);
}

void SvnRaRevProplistFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-rev-proplist";
	struct SvnSession *s;
	svn_revnum_t rev;
	apr_hash_t *props;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnRevnumArgument(theEnv, context, returnValue, fname, "the revision", &rev)) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_ra_rev_proplist(s->ra, rev, &props, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRecords(theEnv, returnValue, fname, SvnPropsToRecords(props, NULL, pool)); }
	svn_pool_destroy(pool);
}

void SvnRaChangeRevPropFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-change-rev-prop";
	struct SvnSession *s;
	svn_revnum_t rev;
	const char *name, *value;
	apr_pool_t *pool;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnRevnumArgument(theEnv, context, returnValue, fname, "the revision", &rev)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the property name", false, &name)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the value", true, &value)) { return; }
	pool = svn_pool_create(SvnRootPool);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_ra_change_rev_prop2(s->ra, rev, name, NULL, value != NULL ? svn_string_create(value, pool) : NULL, pool));
	svn_pool_destroy(pool);
}

void SvnRaHasCapabilityFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-ra-has-capability";
	struct SvnSession *s;
	const char *name;
	svn_boolean_t has;
	apr_pool_t *pool;
	svn_error_t *err;
	int i;

	if ((s = SvnSessionArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the capability", false, &name)) { return; }
	for (i = 0; SvnCapabilityNames[i].name != NULL; i++)
	{
		if (strcmp(SvnCapabilityNames[i].name, name) == 0)
		{
			name = SvnCapabilityNames[i].value;
			break;
		}
	}
	pool = svn_pool_create(SvnRootPool);
	err = svn_ra_has_capability(s->ra, &has, name, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnBool(theEnv, returnValue, has); }
	svn_pool_destroy(pool);
}

/***************************************************************************/
/*                                                                         */
/* Repositories.                                                           */
/*                                                                         */
/* These are the operations of svnadmin: a repository on local disk,       */
/* opened directly, without a working copy or a URL. A test suite makes    */
/* the repositories that it uses this way, so it needs no installed svn.   */
/*                                                                         */
/***************************************************************************/

/* A repository path, absolute and canonical. */
static bool SvnReposPathArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  apr_pool_t *pool,
  const char **out)
{
	return SvnLocalPathArgument(theEnv, context, returnValue, fname, "the repository path", pool, out);
}

/* A path inside the repository, as the filesystem takes it: with a leading
   slash. */
static bool SvnFsPathArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  apr_pool_t *pool,
  const char **out)
{
	const char *text;

	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the path", true, &text))
	{
		return false;
	}
	if (text == NULL || strcmp(text, ".") == 0)
	{
		*out = "/";
		return true;
	}
	text = svn_relpath_canonicalize(text, pool);
	*out = apr_pstrcat(pool, "/", text, (const char *) NULL);
	return true;
}

/* The root of a revision, the youngest if none is given. */
static svn_error_t *SvnReposRoot(
  struct SvnRepos *r,
  svn_revnum_t rev,
  svn_fs_root_t **root,
  apr_pool_t *pool)
{
	if (!SVN_IS_VALID_REVNUM(rev))
	{
		SVN_ERR(svn_fs_youngest_rev(&rev, r->fs, pool));
	}
	return svn_fs_revision_root(root, r->fs, rev, pool);
}

void SvnReposCreateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-create";
	const char *path, *fsType;
	struct SvnRepos *r;
	apr_hash_t *fsConfig;
	apr_pool_t *pool, *scratch;
	svn_error_t *err;

	scratch = svn_pool_create(SvnRootPool);
	if (!SvnReposPathArgument(theEnv, context, returnValue, fname, scratch, &path) ||
	    !SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the filesystem type", &fsType))
	{
		svn_pool_destroy(scratch);
		return;
	}

	if (fsType == NULL || strcmp(fsType, "SVN_FS_TYPE_FSFS") == 0)
	{
		fsType = SVN_FS_TYPE_FSFS;
	}
	else if (strcmp(fsType, "SVN_FS_TYPE_FSX") == 0)
	{
		fsType = SVN_FS_TYPE_FSX;
	}

	pool = svn_pool_create(SvnRootPool);
	r = (struct SvnRepos *) apr_pcalloc(pool, sizeof *r);
	r->pool = pool;
	r->path = apr_pstrdup(pool, path);
	/* The filesystem keeps this hash. So it lives as long as the
	   repository, not as long as this call. */
	fsConfig = apr_hash_make(pool);
	svn_hash_sets(fsConfig, SVN_FS_CONFIG_FS_TYPE, apr_pstrdup(pool, fsType));

	err = svn_repos_create(&r->repos, r->path, NULL, NULL, NULL, fsConfig, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		svn_pool_destroy(pool);
	}
	else
	{
		r->fs = svn_repos_fs(r->repos);
		SvnReturnHandle(theEnv, returnValue, fname, r, SVN_K_REPOS, NULL);
	}
	svn_pool_destroy(scratch);
}

void SvnReposOpenFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-open";
	const char *path;
	struct SvnRepos *r;
	apr_pool_t *pool, *scratch;
	svn_error_t *err;

	scratch = svn_pool_create(SvnRootPool);
	if (!SvnReposPathArgument(theEnv, context, returnValue, fname, scratch, &path))
	{
		svn_pool_destroy(scratch);
		return;
	}

	pool = svn_pool_create(SvnRootPool);
	r = (struct SvnRepos *) apr_pcalloc(pool, sizeof *r);
	r->pool = pool;
	r->path = apr_pstrdup(pool, path);

	err = svn_repos_open3(&r->repos, r->path, NULL, pool, scratch);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		svn_pool_destroy(pool);
	}
	else
	{
		r->fs = svn_repos_fs(r->repos);
		SvnReturnHandle(theEnv, returnValue, fname, r, SVN_K_REPOS, NULL);
	}
	svn_pool_destroy(scratch);
}

void SvnReposFreeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	struct SvnHandle *h;

	if (SvnHandleArgument(theEnv, context, returnValue, "svn-repos-free", SVN_KM(SVN_K_REPOS), "repository", &h) == NULL) { return; }
	SvnHandleRelease(h);
	returnValue->lexemeValue = TrueSymbol(theEnv);
}

/* A wrapper that takes a repository and returns one value calculated in a
   scratch pool. The repository is in scope as r. */
#define SVN_REPOS_GETTER(Cname, fname, body) \
void Cname(Environment *theEnv, UDFContext *context, UDFValue *returnValue) \
{ \
	struct SvnRepos *r; \
	apr_pool_t *pool; \
	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; } \
	pool = svn_pool_create(SvnRootPool); \
	body \
	svn_pool_destroy(pool); \
}

SVN_REPOS_GETTER(SvnReposPathFunction, "svn-repos-path",
	{ SvnReturnText(theEnv, returnValue, svn_repos_path(r->repos, pool)); })

SVN_REPOS_GETTER(SvnReposHookDirFunction, "svn-repos-hook-dir",
	{ SvnReturnText(theEnv, returnValue, svn_repos_hook_dir(r->repos, pool)); })

SVN_REPOS_GETTER(SvnReposConfDirFunction, "svn-repos-conf-dir",
	{ SvnReturnText(theEnv, returnValue, svn_repos_conf_dir(r->repos, pool)); })

SVN_REPOS_GETTER(SvnReposFsTypeFunction, "svn-repos-fs-type",
	{
		const char *type;
		svn_error_t *err = svn_fs_type(&type, svn_repos_db_env(r->repos, pool), pool);
		if (err != NULL) { SvnFailError(theEnv, returnValue, "svn-repos-fs-type", err); }
		else { SvnReturnText(theEnv, returnValue, type); }
	})

SVN_REPOS_GETTER(SvnReposUuidFunction, "svn-repos-uuid",
	{
		const char *uuid;
		svn_error_t *err = svn_fs_get_uuid(r->fs, &uuid, pool);
		if (err != NULL) { SvnFailError(theEnv, returnValue, "svn-repos-uuid", err); }
		else { SvnReturnText(theEnv, returnValue, uuid); }
	})

SVN_REPOS_GETTER(SvnReposYoungestRevFunction, "svn-repos-youngest-rev",
	{
		svn_revnum_t rev;
		svn_error_t *err = svn_fs_youngest_rev(&rev, r->fs, pool);
		if (err != NULL) { SvnFailError(theEnv, returnValue, "svn-repos-youngest-rev", err); }
		else { SvnReturnInteger(theEnv, returnValue, (long long) rev); }
	})

SVN_REPOS_GETTER(SvnReposFileUrlFunction, "svn-repos-file-url",
	{
		const char *url;
		svn_error_t *err = svn_uri_get_file_url_from_dirent(&url, svn_repos_path(r->repos, pool), pool);
		if (err != NULL) { SvnFailError(theEnv, returnValue, "svn-repos-file-url", err); }
		else { SvnReturnText(theEnv, returnValue, url); }
	})

SVN_REPOS_GETTER(SvnReposPackFunction, "svn-repos-pack",
	{ SvnReturnStatus(theEnv, returnValue, "svn-repos-pack", svn_repos_fs_pack2(r->repos, NULL, NULL, NULL, NULL, pool)); })

void SvnReposSetUuidFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-set-uuid";
	struct SvnRepos *r;
	const char *uuid;
	apr_pool_t *pool;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the uuid", &uuid)) { return; }
	pool = svn_pool_create(SvnRootPool);
	SvnReturnStatus(theEnv, returnValue, fname, svn_fs_set_uuid(r->fs, uuid, pool));
	svn_pool_destroy(pool);
}

void SvnReposCheckPathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-check-path";
	struct SvnRepos *r;
	const char *path;
	svn_revnum_t rev;
	svn_fs_root_t *root;
	svn_node_kind_t kind;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnFsPathArgument(theEnv, context, returnValue, fname, pool, &path) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the revision", SVN_INVALID_REVNUM, &rev))
	{
		svn_pool_destroy(pool);
		return;
	}
	err = SvnReposRoot(r, rev, &root, pool);
	if (err == NULL)
	{
		err = svn_fs_check_path(&kind, root, path, pool);
	}
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnEnum(theEnv, returnValue, SvnNodeKindNames, kind); }
	svn_pool_destroy(pool);
}

void SvnReposFileContentsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-file-contents";
	struct SvnRepos *r;
	const char *path;
	svn_revnum_t rev;
	svn_fs_root_t *root;
	svn_stream_t *stream;
	svn_string_t *contents;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnFsPathArgument(theEnv, context, returnValue, fname, pool, &path) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the revision", SVN_INVALID_REVNUM, &rev))
	{
		svn_pool_destroy(pool);
		return;
	}
	err = SvnReposRoot(r, rev, &root, pool);
	if (err == NULL) { err = svn_fs_file_contents(&stream, root, path, pool); }
	if (err == NULL) { err = svn_string_from_stream2(&contents, stream, 0, pool); }
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, contents->data); }
	svn_pool_destroy(pool);
}

void SvnReposDirEntriesFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-dir-entries";
	struct SvnRepos *r;
	const char *path;
	svn_revnum_t rev;
	svn_fs_root_t *root;
	apr_hash_t *entries;
	struct SvnRecords *list = NULL;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnFsPathArgument(theEnv, context, returnValue, fname, pool, &path) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the revision", SVN_INVALID_REVNUM, &rev))
	{
		svn_pool_destroy(pool);
		return;
	}
	err = SvnReposRoot(r, rev, &root, pool);
	if (err == NULL) { err = svn_fs_dir_entries(&entries, root, path, pool); }
	if (err == NULL)
	{
		apr_array_header_t *names = SvnSortedKeys(entries, pool);
		int i;

		list = SvnRecordsNew(&DirentSchema);
		for (i = 0; list != NULL && err == NULL && i < names->nelts; i++)
		{
			const char *name = APR_ARRAY_IDX(names, i, const char *);
			svn_fs_dirent_t *entry = svn_hash_gets(entries, name);
			const char *full = apr_pstrcat(pool, path, strcmp(path, "/") == 0 ? "" : "/", name, (const char *) NULL);
			CLIPSValue *row = SvnRecordsAdd(list);
			svn_filesize_t size;
			svn_revnum_t created;
			apr_hash_t *props;
			svn_string_t *author = NULL, *date = NULL;

			if (row == NULL) { break; }
			SvnSetString(row, DE_PATH, name);
			SvnSetEnum(row, DE_KIND, SvnNodeKindNames, entry->kind);
			SvnSetString(row, DE_ABS_PATH, full);
			if (entry->kind == svn_node_file)
			{
				err = svn_fs_file_length(&size, root, full, pool);
				if (err != NULL) { break; }
				SvnSetInteger(row, DE_SIZE, (long long) size);
			}
			err = svn_fs_node_proplist(&props, root, full, pool);
			if (err != NULL) { break; }
			SvnSetBoolean(row, DE_HAS_PROPS, apr_hash_count(props) > 0);
			err = svn_fs_node_created_rev(&created, root, full, pool);
			if (err != NULL) { break; }
			SvnSetRevision(row, DE_CREATED_REV, created);
			if (SVN_IS_VALID_REVNUM(created))
			{
				err = svn_repos_fs_revision_prop(&author, r->repos, created, SVN_PROP_REVISION_AUTHOR, NULL, NULL, pool);
				if (err == NULL) { err = svn_repos_fs_revision_prop(&date, r->repos, created, SVN_PROP_REVISION_DATE, NULL, NULL, pool); }
				if (err != NULL) { break; }
				SvnSetString(row, DE_AUTHOR, SvnStringValue(author));
				SvnSetDateString(row, DE_DATE, DE_TIME, SvnStringValue(date), pool);
			}
		}
	}
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(list);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, list);
	}
	svn_pool_destroy(pool);
}

void SvnReposNodeProplistFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-node-proplist";
	struct SvnRepos *r;
	const char *path;
	svn_revnum_t rev;
	svn_fs_root_t *root;
	apr_hash_t *props;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnFsPathArgument(theEnv, context, returnValue, fname, pool, &path) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the revision", SVN_INVALID_REVNUM, &rev))
	{
		svn_pool_destroy(pool);
		return;
	}
	err = SvnReposRoot(r, rev, &root, pool);
	if (err == NULL) { err = svn_fs_node_proplist(&props, root, path, pool); }
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRecords(theEnv, returnValue, fname, SvnPropsToRecords(props, path, pool)); }
	svn_pool_destroy(pool);
}

/* The receivers of svn_repos_get_logs5. The changed paths of a revision
   come before the revision. So they are added as they come, and the
   revision is written into them when it arrives. */
static const char *SvnPathChangeAction(
  svn_fs_path_change_kind_t kind)
{
	switch (kind)
	{
		case svn_fs_path_change_add:     return "A";
		case svn_fs_path_change_delete:  return "D";
		case svn_fs_path_change_modify:  return "M";
		case svn_fs_path_change_replace: return "R";
		default:                         return NULL;
	}
}

static svn_error_t *SvnReposPathChangeReceiver(
  void *baton,
  svn_repos_path_change_t *change,
  apr_pool_t *pool)
{
	struct SvnLogBaton *b = (struct SvnLogBaton *) baton;
	CLIPSValue *row = SvnRecordsAdd(b->changes);

	if (row == NULL)
	{
		return svn_error_create(APR_ENOMEM, NULL, "out of memory collecting the log");
	}
	SvnSetSymbol(row, CP_ACTION, SvnPathChangeAction(change->change_kind));
	SvnSetString(row, CP_PATH, change->path.data);
	SvnSetEnum(row, CP_KIND, SvnNodeKindNames, change->node_kind);
	if (change->copyfrom_known && change->copyfrom_path != NULL)
	{
		SvnSetString(row, CP_COPYFROM_PATH, change->copyfrom_path);
		SvnSetRevision(row, CP_COPYFROM_REV, change->copyfrom_rev);
	}
	SvnSetEnum(row, CP_TEXT_MODIFIED, SvnTristateNames, change->text_mod ? svn_tristate_true : svn_tristate_false);
	SvnSetEnum(row, CP_PROPS_MODIFIED, SvnTristateNames, change->prop_mod ? svn_tristate_true : svn_tristate_false);
	b->pending++;
	return SVN_NO_ERROR;
}

static svn_error_t *SvnReposLogReceiver(
  void *baton,
  svn_repos_log_entry_t *e,
  apr_pool_t *pool)
{
	struct SvnLogBaton *b = (struct SvnLogBaton *) baton;
	size_t i;

	if (!SVN_IS_VALID_REVNUM(e->revision))
	{
		return SVN_NO_ERROR;
	}
	for (i = b->changes->count - b->pending; i < b->changes->count; i++)
	{
		SvnSetInteger(SvnRecordsRow(b->changes, i), CP_REVISION, (long long) e->revision);
	}
	if (e->revision == 0 && SvnHashString(e->revprops, SVN_PROP_REVISION_LOG) == NULL)
	{
		b->pending = 0;
		return SVN_NO_ERROR;
	}
	{
		long long changed = (long long) b->pending;
		b->pending = 0;
		return SvnLogEntryRow(b, e->revision, e->revprops, e->has_children, changed, pool);
	}
}

void SvnReposGetLogsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-get-logs";
	struct SvnRepos *r;
	apr_array_header_t *paths;
	svn_revnum_t start, end;
	long long limit;
	svn_boolean_t discoverChangedPaths, strictNodeHistory;
	struct SvnLogBaton b;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTextsArgument(theEnv, context, returnValue, fname, "the paths", pool, &paths) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the start revision", SVN_INVALID_REVNUM, &start) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the end revision", 0, &end) ||
	    !SvnOptionalIntegerArgument(theEnv, context, returnValue, fname, "the limit", 0, &limit) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "discover-changed-paths", TRUE, &discoverChangedPaths) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "strict-node-history", FALSE, &strictNodeHistory))
	{
		svn_pool_destroy(pool);
		return;
	}

	/* nil, or a multifield with nil alone, means every path. */
	if (paths->nelts == 1 && strcmp(APR_ARRAY_IDX(paths, 0, const char *), "nil") == 0)
	{
		paths = NULL;
	}

	b.log = SvnLogRecordsNew();
	b.changes = b.log != NULL ? b.log->related : NULL;
	b.pending = 0;
	if (b.log == NULL)
	{
		SvnFail(theEnv, returnValue, fname, "out of memory");
		svn_pool_destroy(pool);
		return;
	}

	err = svn_repos_get_logs5(r->repos, paths, start, end, (int) limit, strictNodeHistory, FALSE, NULL, NULL, NULL,
	                          discoverChangedPaths ? SvnReposPathChangeReceiver : NULL, &b,
	                          SvnReposLogReceiver, &b, pool);
	if (err != NULL)
	{
		SvnFailError(theEnv, returnValue, fname, err);
		SvnRecordsFree(b.log);
	}
	else
	{
		SvnReturnRecords(theEnv, returnValue, fname, b.log);
	}
	svn_pool_destroy(pool);
}

void SvnReposRevpropGetFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-revprop-get";
	struct SvnRepos *r;
	svn_revnum_t rev;
	const char *name;
	svn_string_t *value;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnRevnumArgument(theEnv, context, returnValue, fname, "the revision", &rev)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the property name", false, &name)) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_repos_fs_revision_prop(&value, r->repos, rev, name, NULL, NULL, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnText(theEnv, returnValue, SvnStringValue(value)); }
	svn_pool_destroy(pool);
}

void SvnReposRevpropSetFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-revprop-set";
	struct SvnRepos *r;
	svn_revnum_t rev;
	const char *name, *value, *author;
	apr_pool_t *pool;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnRevnumArgument(theEnv, context, returnValue, fname, "the revision", &rev)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the property name", false, &name)) { return; }
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the value", true, &value)) { return; }
	if (!SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the author", &author)) { return; }
	pool = svn_pool_create(SvnRootPool);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_repos_fs_change_rev_prop4(r->repos, rev, author, name, NULL,
	                                              value != NULL ? svn_string_create(value, pool) : NULL,
	                                              FALSE, FALSE, NULL, NULL, pool));
	svn_pool_destroy(pool);
}

void SvnReposRevpropListFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-revprop-list";
	struct SvnRepos *r;
	svn_revnum_t rev;
	apr_hash_t *props;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnRevnumArgument(theEnv, context, returnValue, fname, "the revision", &rev)) { return; }
	pool = svn_pool_create(SvnRootPool);
	err = svn_repos_fs_revision_proplist(&props, r->repos, rev, NULL, NULL, pool);
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else { SvnReturnRecords(theEnv, returnValue, fname, SvnPropsToRecords(props, NULL, pool)); }
	svn_pool_destroy(pool);
}

void SvnReposRecoverFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-recover";
	const char *path;
	apr_pool_t *pool;

	pool = svn_pool_create(SvnRootPool);
	if (!SvnReposPathArgument(theEnv, context, returnValue, fname, pool, &path))
	{
		svn_pool_destroy(pool);
		return;
	}
	SvnReturnStatus(theEnv, returnValue, fname, svn_repos_recover4(path, TRUE, NULL, NULL, NULL, NULL, pool));
	svn_pool_destroy(pool);
}

void SvnReposVerifyFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-verify";
	struct SvnRepos *r;
	svn_revnum_t start, end;
	apr_pool_t *pool;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	if (!SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the start revision", 0, &start)) { return; }
	if (!SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the end revision", SVN_INVALID_REVNUM, &end)) { return; }
	pool = svn_pool_create(SvnRootPool);
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_repos_verify_fs3(r->repos, start, end, FALSE, FALSE, NULL, NULL, NULL, NULL, NULL, NULL, pool));
	svn_pool_destroy(pool);
}

void SvnReposHotcopyFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-hotcopy";
	const char *src, *dst;
	svn_boolean_t incremental;
	apr_pool_t *pool;

	pool = svn_pool_create(SvnRootPool);
	if (!SvnLocalPathArgument(theEnv, context, returnValue, fname, "the source path", pool, &src) ||
	    !SvnLocalPathArgument(theEnv, context, returnValue, fname, "the destination path", pool, &dst) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "incremental", FALSE, &incremental))
	{
		svn_pool_destroy(pool);
		return;
	}
	SvnReturnStatus(theEnv, returnValue, fname,
	                svn_repos_hotcopy3(src, dst, FALSE, incremental, NULL, NULL, NULL, NULL, pool));
	svn_pool_destroy(pool);
}

void SvnReposDeleteFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-delete";
	const char *path;
	apr_pool_t *pool;

	pool = svn_pool_create(SvnRootPool);
	if (!SvnReposPathArgument(theEnv, context, returnValue, fname, pool, &path))
	{
		svn_pool_destroy(pool);
		return;
	}
	SvnReturnStatus(theEnv, returnValue, fname, svn_repos_delete(path, pool));
	svn_pool_destroy(pool);
}

void SvnReposDumpFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-dump";
	struct SvnRepos *r;
	const char *file;
	svn_revnum_t start, end;
	svn_boolean_t incremental, useDeltas;
	svn_stream_t *stream;
	svn_stringbuf_t *sb = NULL;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the file", true, &file) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the start revision", 0, &start) ||
	    !SvnOptionalRevnumArgument(theEnv, context, returnValue, fname, "the end revision", SVN_INVALID_REVNUM, &end) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "incremental", FALSE, &incremental) ||
	    !SvnOptionalBooleanArgument(theEnv, context, returnValue, fname, "use-deltas", FALSE, &useDeltas))
	{
		svn_pool_destroy(pool);
		return;
	}

	if (file != NULL)
	{
		apr_file_t *f;

		err = svn_io_file_open(&f, svn_dirent_internal_style(file, pool),
		                       APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BUFFERED, APR_OS_DEFAULT, pool);
		stream = err == NULL ? svn_stream_from_aprfile2(f, FALSE, pool) : NULL;
	}
	else
	{
		sb = svn_stringbuf_create_empty(pool);
		stream = svn_stream_from_stringbuf(sb, pool);
		err = NULL;
	}
	if (err == NULL)
	{
		err = svn_repos_dump_fs4(r->repos, stream, start, end, incremental, useDeltas, TRUE, TRUE,
		                         NULL, NULL, NULL, NULL, NULL, NULL, pool);
	}
	if (err == NULL)
	{
		err = svn_stream_close(stream);
	}
	if (err != NULL) { SvnFailError(theEnv, returnValue, fname, err); }
	else if (sb != NULL) { SvnReturnText(theEnv, returnValue, sb->data); }
	else { returnValue->lexemeValue = TrueSymbol(theEnv); }
	svn_pool_destroy(pool);
}

void SvnReposLoadFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "svn-repos-load";
	struct SvnRepos *r;
	const char *file, *parentDir;
	svn_stream_t *stream;
	apr_pool_t *pool;
	svn_error_t *err;

	if ((r = SvnReposArgument(theEnv, context, returnValue, fname)) == NULL) { return; }
	pool = svn_pool_create(SvnRootPool);
	if (!SvnTextArgument(theEnv, context, returnValue, fname, "the file", false, &file) ||
	    !SvnOptionalTextArgument(theEnv, context, returnValue, fname, "the parent directory", &parentDir))
	{
		svn_pool_destroy(pool);
		return;
	}

	err = svn_stream_open_readonly(&stream, svn_dirent_internal_style(file, pool), pool, pool);
	if (err == NULL)
	{
		err = svn_repos_load_fs6(r->repos, stream, SVN_INVALID_REVNUM, SVN_INVALID_REVNUM, svn_repos_load_uuid_default,
		                         parentDir, FALSE, FALSE, FALSE, FALSE, FALSE, NULL, NULL, NULL, NULL, pool);
	}
	SvnReturnStatus(theEnv, returnValue, fname, err);
	svn_pool_destroy(pool);
}

/***************************************************************************/
/*                                                                         */
/* Registration.                                                           */
/*                                                                         */
/* The doc-group and doc annotations are read by docs/gen-api-docs.sh.    */
/*                                                                         */
/***************************************************************************/

void UserFunctions(
  Environment *env)
{
	svn_error_t *err;

	SvnEnv = env;

	/* APR and the two module loaders are initialized one time. The root
	   pool is the parent of every pool that this file makes. Neither is
	   released: the environment is not usable without them. */
	if (SvnRootPool == NULL)
	{
		apr_initialize();
		SvnRootPool = svn_pool_create(NULL);
		/* The UTF-8 translation that every path goes through. Without
		   this, the first conversion is an assertion. */
		svn_utf_initialize2(FALSE, SvnRootPool);
		err = svn_fs_initialize(SvnRootPool);
		if (err == NULL) { err = svn_ra_initialize(SvnRootPool); }
		if (err != NULL)
		{
			SvnRecordError(err);
			fprintf(stderr, "CLIPSVN: Subversion could not be initialized: %s\n", SvnLastMessage);
		}
	}

	/* doc-group: The library */
	AddUDF(env,"svn-version","s",0,0,NULL,SvnVersionFunction,"SvnVersionFunction",NULL);  /* doc: -> string */
	AddUDF(env,"svn-version-number","l",0,0,NULL,SvnVersionNumberFunction,"SvnVersionNumberFunction",NULL);  /* doc: -> integer */
	AddUDF(env,"svn-built-version","l",0,0,NULL,SvnBuiltVersionFunction,"SvnBuiltVersionFunction",NULL);  /* doc: -> integer */
	AddUDF(env,"svn-version-tag","sy",0,0,NULL,SvnVersionTagFunction,"SvnVersionTagFunction",NULL);  /* doc: -> string */
	AddUDF(env,"svn-ra-modules","bs",0,0,NULL,SvnRaModulesFunction,"SvnRaModulesFunction",NULL);  /* doc: -> string */
	AddUDF(env,"svn-fs-modules","bs",0,0,NULL,SvnFsModulesFunction,"SvnFsModulesFunction",NULL);  /* doc: -> string */
	AddUDF(env,"svn-handle-count","l",0,0,NULL,SvnHandleCountFunction,"SvnHandleCountFunction",NULL);  /* doc: -> integer */
	AddUDF(env,"svn-handle-kinds","m",0,0,NULL,SvnHandleKindsFunction,"SvnHandleKindsFunction",NULL);  /* doc: -> multifield */
	AddUDF(env,"svn-error-last","sy",0,0,NULL,SvnErrorLastFunction,"SvnErrorLastFunction",NULL);  /* doc: -> string */
	AddUDF(env,"svn-error-last-code","l",0,0,NULL,SvnErrorLastCodeFunction,"SvnErrorLastCodeFunction",NULL);  /* doc: -> integer */
	AddUDF(env,"svn-error-last-name","y",0,0,NULL,SvnErrorLastNameFunction,"SvnErrorLastNameFunction",NULL);  /* doc: -> symbol */

	/* doc-group: Paths, URLs, times and words */
	AddUDF(env,"svn-path-is-url","b",1,1,";sy",SvnPathIsUrlFunction,"SvnPathIsUrlFunction",NULL);  /* doc: text -> boolean */
	AddUDF(env,"svn-uri-canonicalize","bs",1,1,";sy",SvnUriCanonicalizeFunction,"SvnUriCanonicalizeFunction",NULL);  /* doc: url -> string */
	AddUDF(env,"svn-dirent-canonicalize","bs",1,1,";sy",SvnDirentCanonicalizeFunction,"SvnDirentCanonicalizeFunction",NULL);  /* doc: path -> string */
	AddUDF(env,"svn-relpath-canonicalize","bs",1,1,";sy",SvnRelpathCanonicalizeFunction,"SvnRelpathCanonicalizeFunction",NULL);  /* doc: path -> string */
	AddUDF(env,"svn-dirent-get-absolute","bs",1,1,";sy",SvnDirentGetAbsoluteFunction,"SvnDirentGetAbsoluteFunction",NULL);  /* doc: path -> string */
	AddUDF(env,"svn-dirent-is-absolute","b",1,1,";sy",SvnDirentIsAbsoluteFunction,"SvnDirentIsAbsoluteFunction",NULL);  /* doc: path -> boolean */
	AddUDF(env,"svn-uri-is-canonical","b",1,1,";sy",SvnUriIsCanonicalFunction,"SvnUriIsCanonicalFunction",NULL);  /* doc: url -> boolean */
	AddUDF(env,"svn-dirent-is-canonical","b",1,1,";sy",SvnDirentIsCanonicalFunction,"SvnDirentIsCanonicalFunction",NULL);  /* doc: path -> boolean */
	AddUDF(env,"svn-uri-basename","bs",1,1,";sy",SvnUriBasenameFunction,"SvnUriBasenameFunction",NULL);  /* doc: url -> string */
	AddUDF(env,"svn-uri-dirname","bs",1,1,";sy",SvnUriDirnameFunction,"SvnUriDirnameFunction",NULL);  /* doc: url -> string */
	AddUDF(env,"svn-dirent-basename","bs",1,1,";sy",SvnDirentBasenameFunction,"SvnDirentBasenameFunction",NULL);  /* doc: path -> string */
	AddUDF(env,"svn-dirent-dirname","bs",1,1,";sy",SvnDirentDirnameFunction,"SvnDirentDirnameFunction",NULL);  /* doc: path -> string */
	AddUDF(env,"svn-uri-join","bs",2,2,";sy;sy",SvnUriJoinFunction,"SvnUriJoinFunction",NULL);  /* doc: url component -> string */
	AddUDF(env,"svn-dirent-join","bs",2,2,";sy;sy",SvnDirentJoinFunction,"SvnDirentJoinFunction",NULL);  /* doc: path component -> string */
	AddUDF(env,"svn-uri-skip-ancestor","bsy",2,2,";sy;sy",SvnUriSkipAncestorFunction,"SvnUriSkipAncestorFunction",NULL);  /* doc: parent-url child-url -> string */
	AddUDF(env,"svn-dirent-skip-ancestor","bsy",2,2,";sy;sy",SvnDirentSkipAncestorFunction,"SvnDirentSkipAncestorFunction",NULL);  /* doc: parent-path child-path -> string */
	AddUDF(env,"svn-file-url-from-dirent","bs",1,1,";sy",SvnFileUrlFromDirentFunction,"SvnFileUrlFromDirentFunction",NULL);  /* doc: path -> string */
	AddUDF(env,"svn-dirent-from-file-url","bs",1,1,";sy",SvnDirentFromFileUrlFunction,"SvnDirentFromFileUrlFunction",NULL);  /* doc: url -> string */
	AddUDF(env,"svn-time-to-cstring","bs",1,1,";l",SvnTimeToCstringFunction,"SvnTimeToCstringFunction",NULL);  /* doc: seconds -> string */
	AddUDF(env,"svn-time-from-cstring","bl",1,1,";sy",SvnTimeFromCstringFunction,"SvnTimeFromCstringFunction",NULL);  /* doc: date -> integer */
	AddUDF(env,"svn-depth-to-word","bs",1,1,";lsy",SvnDepthToWordFunction,"SvnDepthToWordFunction",NULL);  /* doc: depth -> string */
	AddUDF(env,"svn-depth-from-word","by",1,1,";sy",SvnDepthFromWordFunction,"SvnDepthFromWordFunction",NULL);  /* doc: word -> symbol */
	AddUDF(env,"svn-node-kind-to-word","bs",1,1,";lsy",SvnNodeKindToWordFunction,"SvnNodeKindToWordFunction",NULL);  /* doc: kind -> string */
	AddUDF(env,"svn-node-kind-from-word","by",1,1,";sy",SvnNodeKindFromWordFunction,"SvnNodeKindFromWordFunction",NULL);  /* doc: word -> symbol */
	AddUDF(env,"svn-opt-parse-revision","bm",1,1,";sy",SvnOptParseRevisionFunction,"SvnOptParseRevisionFunction",NULL);  /* doc: text -> multifield */

	/* doc-group: Contexts */
	AddUDF(env,"svn-client-create-context","be",0,1,";sy",SvnClientCreateContextFunction,"SvnClientCreateContextFunction",NULL);  /* doc: config-dir? -> handle */
	AddUDF(env,"svn-client-context-free","b",1,1,";e",SvnClientContextFreeFunction,"SvnClientContextFreeFunction",NULL);  /* doc: ctx -> boolean */
	AddUDF(env,"svn-client-set-auth","b",2,3,";e;sy;sy",SvnClientSetAuthFunction,"SvnClientSetAuthFunction",NULL);  /* doc: ctx username password? -> boolean */
	AddUDF(env,"svn-client-set-config","b",4,4,";e;sy;sy;sy",SvnClientSetConfigFunction,"SvnClientSetConfigFunction",NULL);  /* doc: ctx section option value -> boolean */
	AddUDF(env,"svn-client-get-config","bsy",3,3,";e;sy;sy",SvnClientGetConfigFunction,"SvnClientGetConfigFunction",NULL);  /* doc: ctx section option -> string */
	AddUDF(env,"svn-client-notifications","be",1,1,";e",SvnClientNotificationsFunction,"SvnClientNotificationsFunction",NULL);  /* doc: ctx -> records */
	AddUDF(env,"svn-client-notification-count","bl",1,1,";e",SvnClientNotificationCountFunction,"SvnClientNotificationCountFunction",NULL);  /* doc: ctx -> integer */

	/* doc-group: Working copies */
	AddUDF(env,"svn-client-checkout","bl",3,6,";e;sy;sy;lsy;lsy;b",SvnClientCheckoutFunction,"SvnClientCheckoutFunction",NULL);  /* doc: ctx url path revision? depth? ignore-externals? -> integer */
	AddUDF(env,"svn-client-update","blmy",2,6,";e;msy;lsy;lsy;b;b",SvnClientUpdateFunction,"SvnClientUpdateFunction",NULL);  /* doc: ctx paths revision? depth? depth-is-sticky? ignore-externals? -> integer or multifield */
	AddUDF(env,"svn-client-switch","bl",3,6,";e;sy;sy;lsy;lsy;b",SvnClientSwitchFunction,"SvnClientSwitchFunction",NULL);  /* doc: ctx path url revision? depth? ignore-externals? -> integer */
	AddUDF(env,"svn-client-add","b",2,6,";e;sy;lsy;b;b;b",SvnClientAddFunction,"SvnClientAddFunction",NULL);  /* doc: ctx path depth? force? no-ignore? add-parents? -> boolean */
	AddUDF(env,"svn-client-mkdir","bl",2,4,";e;msy;b;sy",SvnClientMkdirFunction,"SvnClientMkdirFunction",NULL);  /* doc: ctx paths make-parents? message? -> integer or TRUE */
	AddUDF(env,"svn-client-delete","bl",2,5,";e;msy;b;b;sy",SvnClientDeleteFunction,"SvnClientDeleteFunction",NULL);  /* doc: ctx paths force? keep-local? message? -> integer or TRUE */
	AddUDF(env,"svn-client-copy","bl",3,7,";e;sy;sy;lsy;b;b;sy",SvnClientCopyFunction,"SvnClientCopyFunction",NULL);  /* doc: ctx source destination source-revision? copy-as-child? make-parents? message? -> integer or TRUE */
	AddUDF(env,"svn-client-move","bl",3,6,";e;sy;sy;b;b;sy",SvnClientMoveFunction,"SvnClientMoveFunction",NULL);  /* doc: ctx source destination move-as-child? make-parents? message? -> integer or TRUE */
	AddUDF(env,"svn-client-commit","bly",3,5,";e;msy;sy;lsy;b",SvnClientCommitFunction,"SvnClientCommitFunction",NULL);  /* doc: ctx paths message depth? keep-locks? -> integer */
	AddUDF(env,"svn-client-revert","b",2,3,";e;msy;lsy",SvnClientRevertFunction,"SvnClientRevertFunction",NULL);  /* doc: ctx paths depth? -> boolean */
	AddUDF(env,"svn-client-resolve","b",3,4,";e;sy;lsy;lsy",SvnClientResolveFunction,"SvnClientResolveFunction",NULL);  /* doc: ctx path choice depth? -> boolean */
	AddUDF(env,"svn-client-cleanup","b",2,6,";e;sy;b;b;b;b",SvnClientCleanupFunction,"SvnClientCleanupFunction",NULL);  /* doc: ctx path break-locks? fix-timestamps? vacuum-pristines? include-externals? -> boolean */
	AddUDF(env,"svn-client-vacuum","b",2,7,";e;sy;b;b;b;b;b",SvnClientVacuumFunction,"SvnClientVacuumFunction",NULL);  /* doc: ctx path remove-unversioned? remove-ignored? fix-timestamps? vacuum-pristines? include-externals? -> boolean */
	AddUDF(env,"svn-client-relocate","b",4,5,";e;sy;sy;sy;b",SvnClientRelocateFunction,"SvnClientRelocateFunction",NULL);  /* doc: ctx path from-url to-url ignore-externals? -> boolean */
	AddUDF(env,"svn-client-upgrade","b",2,2,";e;sy",SvnClientUpgradeFunction,"SvnClientUpgradeFunction",NULL);  /* doc: ctx path -> boolean */
	AddUDF(env,"svn-client-lock","b",2,4,";e;msy;sy;b",SvnClientLockFunction,"SvnClientLockFunction",NULL);  /* doc: ctx targets comment? steal? -> boolean */
	AddUDF(env,"svn-client-unlock","b",2,3,";e;msy;b",SvnClientUnlockFunction,"SvnClientUnlockFunction",NULL);  /* doc: ctx targets break? -> boolean */
	AddUDF(env,"svn-client-export","bly",3,7,";e;sy;sy;lsy;b;b;lsy",SvnClientExportFunction,"SvnClientExportFunction",NULL);  /* doc: ctx source destination revision? overwrite? ignore-externals? depth? -> integer */
	AddUDF(env,"svn-client-import","bly",4,6,";e;sy;sy;sy;lsy;b",SvnClientImportFunction,"SvnClientImportFunction",NULL);  /* doc: ctx path url message depth? no-ignore? -> integer */
	AddUDF(env,"svn-client-cat","bs",2,5,";e;sy;lsy;lsy;b",SvnClientCatFunction,"SvnClientCatFunction",NULL);  /* doc: ctx target revision? peg-revision? expand-keywords? -> string */
	AddUDF(env,"svn-client-get-wc-root","bs",2,2,";e;sy",SvnClientGetWcRootFunction,"SvnClientGetWcRootFunction",NULL);  /* doc: ctx path -> string */
	AddUDF(env,"svn-client-get-repos-root","bm",2,2,";e;sy",SvnClientGetReposRootFunction,"SvnClientGetReposRootFunction",NULL);  /* doc: ctx target -> multifield */
	AddUDF(env,"svn-client-url-from-path","bsy",2,2,";e;sy",SvnClientUrlFromPathFunction,"SvnClientUrlFromPathFunction",NULL);  /* doc: ctx target -> string */
	AddUDF(env,"svn-client-min-max-revisions","bm",2,3,";e;sy;b",SvnClientMinMaxRevisionsFunction,"SvnClientMinMaxRevisionsFunction",NULL);  /* doc: ctx path committed? -> multifield */

	/* doc-group: Status, log, info, list, blame and diff */
	AddUDF(env,"svn-client-status","be",2,8,";e;sy;lsy;lsy;b;b;b;b",SvnClientStatusFunction,"SvnClientStatusFunction",NULL);  /* doc: ctx path revision? depth? get-all? check-out-of-date? no-ignore? ignore-externals? -> records */
	AddUDF(env,"svn-client-log","be",2,8,";e;msy;lsy;lsy;l;b;b;lsy",SvnClientLogFunction,"SvnClientLogFunction",NULL);  /* doc: ctx targets start? end? limit? discover-changed-paths? strict-node-history? peg-revision? -> records */
	AddUDF(env,"svn-log-changed-paths","be",1,1,";e",SvnLogChangedPathsFunction,"SvnLogChangedPathsFunction",NULL);  /* doc: log -> records */
	AddUDF(env,"svn-client-info","be",2,5,";e;sy;lsy;lsy;lsy",SvnClientInfoFunction,"SvnClientInfoFunction",NULL);  /* doc: ctx target revision? peg-revision? depth? -> records */
	AddUDF(env,"svn-client-list","be",2,6,";e;sy;lsy;lsy;lsy;b",SvnClientListFunction,"SvnClientListFunction",NULL);  /* doc: ctx target revision? peg-revision? depth? fetch-locks? -> records */
	AddUDF(env,"svn-client-blame","be",2,5,";e;sy;lsy;lsy;lsy",SvnClientBlameFunction,"SvnClientBlameFunction",NULL);  /* doc: ctx target start? end? peg-revision? -> records */
	AddUDF(env,"svn-client-diff","bs",5,8,";e;sy;lsy;sy;lsy;lsy;b;b",SvnClientDiffFunction,"SvnClientDiffFunction",NULL);  /* doc: ctx target1 revision1 target2 revision2 depth? ignore-ancestry? git-format? -> string */
	AddUDF(env,"svn-client-diff-summarize","be",5,7,";e;sy;lsy;sy;lsy;lsy;b",SvnClientDiffSummarizeFunction,"SvnClientDiffSummarizeFunction",NULL);  /* doc: ctx target1 revision1 target2 revision2 depth? ignore-ancestry? -> records */

	/* doc-group: Merging */
	AddUDF(env,"svn-client-merge","b",6,11,";e;sy;lsy;sy;lsy;sy;lsy;b;b;b;b",SvnClientMergeFunction,"SvnClientMergeFunction",NULL);  /* doc: ctx source1 revision1 source2 revision2 target depth? ignore-ancestry? force-delete? record-only? dry-run? -> boolean */
	AddUDF(env,"svn-client-merge-peg","b",3,11,";e;sy;sy;lsy;lsy;lsy;lsy;b;b;b;b",SvnClientMergePegFunction,"SvnClientMergePegFunction",NULL);  /* doc: ctx source target start? end? peg-revision? depth? ignore-ancestry? force-delete? record-only? dry-run? -> boolean */

	/* doc-group: Properties */
	AddUDF(env,"svn-client-propget","bsy",3,5,";e;sy;sy;lsy;lsy",SvnClientPropgetFunction,"SvnClientPropgetFunction",NULL);  /* doc: ctx name target revision? peg-revision? -> string */
	AddUDF(env,"svn-client-proplist","be",2,5,";e;sy;lsy;lsy;lsy",SvnClientProplistFunction,"SvnClientProplistFunction",NULL);  /* doc: ctx target revision? peg-revision? depth? -> records */
	AddUDF(env,"svn-client-propset-local","b",4,6,";e;sy;sy;msy;lsy;b",SvnClientPropsetLocalFunction,"SvnClientPropsetLocalFunction",NULL);  /* doc: ctx name value targets depth? skip-checks? -> boolean */
	AddUDF(env,"svn-client-propset-remote","bly",5,6,";e;sy;sy;sy;l;sy",SvnClientPropsetRemoteFunction,"SvnClientPropsetRemoteFunction",NULL);  /* doc: ctx name value url base-revision message? -> integer */
	AddUDF(env,"svn-client-revprop-get","bsy",4,4,";e;sy;sy;lsy",SvnClientRevpropGetFunction,"SvnClientRevpropGetFunction",NULL);  /* doc: ctx name url revision -> string */
	AddUDF(env,"svn-client-revprop-set","bly",5,6,";e;sy;sy;sy;lsy;b",SvnClientRevpropSetFunction,"SvnClientRevpropSetFunction",NULL);  /* doc: ctx name value url revision force? -> integer */
	AddUDF(env,"svn-client-revprop-list","be",3,3,";e;sy;lsy",SvnClientRevpropListFunction,"SvnClientRevpropListFunction",NULL);  /* doc: ctx url revision -> records */

	/* doc-group: Repository access sessions */
	AddUDF(env,"svn-client-open-ra-session","be",2,2,";e;sy",SvnClientOpenRaSessionFunction,"SvnClientOpenRaSessionFunction",NULL);  /* doc: ctx url -> handle */
	AddUDF(env,"svn-ra-session-free","b",1,1,";e",SvnRaSessionFreeFunction,"SvnRaSessionFreeFunction",NULL);  /* doc: session -> boolean */
	AddUDF(env,"svn-ra-get-latest-revnum","bl",1,1,";e",SvnRaGetLatestRevnumFunction,"SvnRaGetLatestRevnumFunction",NULL);  /* doc: session -> integer */
	AddUDF(env,"svn-ra-get-uuid","bs",1,1,";e",SvnRaGetUuidFunction,"SvnRaGetUuidFunction",NULL);  /* doc: session -> string */
	AddUDF(env,"svn-ra-get-repos-root","bs",1,1,";e",SvnRaGetReposRootFunction,"SvnRaGetReposRootFunction",NULL);  /* doc: session -> string */
	AddUDF(env,"svn-ra-get-session-url","bs",1,1,";e",SvnRaGetSessionUrlFunction,"SvnRaGetSessionUrlFunction",NULL);  /* doc: session -> string */
	AddUDF(env,"svn-ra-reparent","b",2,2,";e;sy",SvnRaReparentFunction,"SvnRaReparentFunction",NULL);  /* doc: session url -> boolean */
	AddUDF(env,"svn-ra-check-path","by",2,3,";e;sy;ly",SvnRaCheckPathFunction,"SvnRaCheckPathFunction",NULL);  /* doc: session path revision? -> symbol */
	AddUDF(env,"svn-ra-stat","be",2,3,";e;sy;ly",SvnRaStatFunction,"SvnRaStatFunction",NULL);  /* doc: session path revision? -> records */
	AddUDF(env,"svn-ra-get-file","bs",2,3,";e;sy;ly",SvnRaGetFileFunction,"SvnRaGetFileFunction",NULL);  /* doc: session path revision? -> string */
	AddUDF(env,"svn-ra-get-file-props","be",2,3,";e;sy;ly",SvnRaGetFilePropsFunction,"SvnRaGetFilePropsFunction",NULL);  /* doc: session path revision? -> records */
	AddUDF(env,"svn-ra-get-dir","be",2,3,";e;sy;ly",SvnRaGetDirFunction,"SvnRaGetDirFunction",NULL);  /* doc: session path revision? -> records */
	AddUDF(env,"svn-ra-get-log","be",2,7,";e;msy;ly;ly;l;b;b",SvnRaGetLogFunction,"SvnRaGetLogFunction",NULL);  /* doc: session paths start? end? limit? discover-changed-paths? strict-node-history? -> records */
	AddUDF(env,"svn-ra-get-dated-revision","bl",2,2,";e;sy",SvnRaGetDatedRevisionFunction,"SvnRaGetDatedRevisionFunction",NULL);  /* doc: session date -> integer */
	AddUDF(env,"svn-ra-rev-prop","bsy",3,3,";e;l;sy",SvnRaRevPropFunction,"SvnRaRevPropFunction",NULL);  /* doc: session revision name -> string */
	AddUDF(env,"svn-ra-rev-proplist","be",2,2,";e;l",SvnRaRevProplistFunction,"SvnRaRevProplistFunction",NULL);  /* doc: session revision -> records */
	AddUDF(env,"svn-ra-change-rev-prop","b",4,4,";e;l;sy;sy",SvnRaChangeRevPropFunction,"SvnRaChangeRevPropFunction",NULL);  /* doc: session revision name value -> boolean */
	AddUDF(env,"svn-ra-has-capability","b",2,2,";e;sy",SvnRaHasCapabilityFunction,"SvnRaHasCapabilityFunction",NULL);  /* doc: session capability -> boolean */

	/* doc-group: Repositories */
	AddUDF(env,"svn-repos-create","be",1,2,";sy;sy",SvnReposCreateFunction,"SvnReposCreateFunction",NULL);  /* doc: path fs-type? -> handle */
	AddUDF(env,"svn-repos-open","be",1,1,";sy",SvnReposOpenFunction,"SvnReposOpenFunction",NULL);  /* doc: path -> handle */
	AddUDF(env,"svn-repos-free","b",1,1,";e",SvnReposFreeFunction,"SvnReposFreeFunction",NULL);  /* doc: repos -> boolean */
	AddUDF(env,"svn-repos-path","bs",1,1,";e",SvnReposPathFunction,"SvnReposPathFunction",NULL);  /* doc: repos -> string */
	AddUDF(env,"svn-repos-file-url","bs",1,1,";e",SvnReposFileUrlFunction,"SvnReposFileUrlFunction",NULL);  /* doc: repos -> string */
	AddUDF(env,"svn-repos-hook-dir","bs",1,1,";e",SvnReposHookDirFunction,"SvnReposHookDirFunction",NULL);  /* doc: repos -> string */
	AddUDF(env,"svn-repos-conf-dir","bs",1,1,";e",SvnReposConfDirFunction,"SvnReposConfDirFunction",NULL);  /* doc: repos -> string */
	AddUDF(env,"svn-repos-fs-type","bs",1,1,";e",SvnReposFsTypeFunction,"SvnReposFsTypeFunction",NULL);  /* doc: repos -> string */
	AddUDF(env,"svn-repos-uuid","bs",1,1,";e",SvnReposUuidFunction,"SvnReposUuidFunction",NULL);  /* doc: repos -> string */
	AddUDF(env,"svn-repos-set-uuid","b",1,2,";e;sy",SvnReposSetUuidFunction,"SvnReposSetUuidFunction",NULL);  /* doc: repos uuid? -> boolean */
	AddUDF(env,"svn-repos-youngest-rev","bl",1,1,";e",SvnReposYoungestRevFunction,"SvnReposYoungestRevFunction",NULL);  /* doc: repos -> integer */
	AddUDF(env,"svn-repos-check-path","by",2,3,";e;sy;ly",SvnReposCheckPathFunction,"SvnReposCheckPathFunction",NULL);  /* doc: repos path revision? -> symbol */
	AddUDF(env,"svn-repos-file-contents","bs",2,3,";e;sy;ly",SvnReposFileContentsFunction,"SvnReposFileContentsFunction",NULL);  /* doc: repos path revision? -> string */
	AddUDF(env,"svn-repos-dir-entries","be",2,3,";e;sy;ly",SvnReposDirEntriesFunction,"SvnReposDirEntriesFunction",NULL);  /* doc: repos path revision? -> records */
	AddUDF(env,"svn-repos-node-proplist","be",2,3,";e;sy;ly",SvnReposNodeProplistFunction,"SvnReposNodeProplistFunction",NULL);  /* doc: repos path revision? -> records */
	AddUDF(env,"svn-repos-get-logs","be",2,7,";e;msy;ly;ly;l;b;b",SvnReposGetLogsFunction,"SvnReposGetLogsFunction",NULL);  /* doc: repos paths start? end? limit? discover-changed-paths? strict-node-history? -> records */
	AddUDF(env,"svn-repos-revprop-get","bsy",3,3,";e;l;sy",SvnReposRevpropGetFunction,"SvnReposRevpropGetFunction",NULL);  /* doc: repos revision name -> string */
	AddUDF(env,"svn-repos-revprop-set","b",4,5,";e;l;sy;sy;sy",SvnReposRevpropSetFunction,"SvnReposRevpropSetFunction",NULL);  /* doc: repos revision name value author? -> boolean */
	AddUDF(env,"svn-repos-revprop-list","be",2,2,";e;l",SvnReposRevpropListFunction,"SvnReposRevpropListFunction",NULL);  /* doc: repos revision -> records */
	AddUDF(env,"svn-repos-recover","b",1,1,";sy",SvnReposRecoverFunction,"SvnReposRecoverFunction",NULL);  /* doc: path -> boolean */
	AddUDF(env,"svn-repos-verify","b",1,3,";e;ly;ly",SvnReposVerifyFunction,"SvnReposVerifyFunction",NULL);  /* doc: repos start? end? -> boolean */
	AddUDF(env,"svn-repos-pack","b",1,1,";e",SvnReposPackFunction,"SvnReposPackFunction",NULL);  /* doc: repos -> boolean */
	AddUDF(env,"svn-repos-hotcopy","b",2,3,";sy;sy;b",SvnReposHotcopyFunction,"SvnReposHotcopyFunction",NULL);  /* doc: source destination incremental? -> boolean */
	AddUDF(env,"svn-repos-delete","b",1,1,";sy",SvnReposDeleteFunction,"SvnReposDeleteFunction",NULL);  /* doc: path -> boolean */
	AddUDF(env,"svn-repos-dump","bs",2,6,";e;sy;ly;ly;b;b",SvnReposDumpFunction,"SvnReposDumpFunction",NULL);  /* doc: repos file start? end? incremental? use-deltas? -> string or TRUE */
	AddUDF(env,"svn-repos-load","b",2,3,";e;sy;sy",SvnReposLoadFunction,"SvnReposLoadFunction",NULL);  /* doc: repos file parent-dir? -> boolean */

	/* doc-group: Records */
	AddUDF(env,"svn-records-kind","by",1,1,";e",SvnRecordsKindFunction,"SvnRecordsKindFunction",NULL);  /* doc: records -> symbol */
	AddUDF(env,"svn-records-count","bl",1,1,";e",SvnRecordsCountFunction,"SvnRecordsCountFunction",NULL);  /* doc: records -> integer */
	AddUDF(env,"svn-records-fields","bm",1,1,";e",SvnRecordsFieldsFunction,"SvnRecordsFieldsFunction",NULL);  /* doc: records -> multifield */
	AddUDF(env,"svn-record-byindex","bm",2,2,";e;l",SvnRecordByindexFunction,"SvnRecordByindexFunction",NULL);  /* doc: records index -> multifield */
	AddUDF(env,"svn-record-field","blsy",3,3,";e;l;sy",SvnRecordFieldFunction,"SvnRecordFieldFunction",NULL);  /* doc: records index field -> value */
	AddUDF(env,"svn-record-to-fact","bf",3,3,";e;l;y",SvnRecordToFactFunction,"SvnRecordToFactFunction",NULL);  /* doc: records index deftemplate -> fact */
	AddUDF(env,"svn-records-to-facts","bl",2,2,";e;y",SvnRecordsToFactsFunction,"SvnRecordsToFactsFunction",NULL);  /* doc: records deftemplate -> integer */
	AddUDF(env,"svn-record-to-instance","bi",3,4,";e;l;y;sy",SvnRecordToInstanceFunction,"SvnRecordToInstanceFunction",NULL);  /* doc: records index defclass name? -> instance */
	AddUDF(env,"svn-records-to-instances","bl",2,2,";e;y",SvnRecordsToInstancesFunction,"SvnRecordsToInstancesFunction",NULL);  /* doc: records defclass -> integer */
	AddUDF(env,"svn-records-free","b",1,1,";e",SvnRecordsFreeFunction,"SvnRecordsFreeFunction",NULL);  /* doc: records -> boolean */
}
