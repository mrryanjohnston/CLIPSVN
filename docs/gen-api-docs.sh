#!/bin/sh
# Regenerates API.md.
#
# The Subversion headers have doxygen comments. But a CLIPS function does not do
# exactly what its C function does. An argument is optional here, or a value
# returns as a multifield there. So the descriptions are written by hand, in
# docs/api-descriptions.md. This script keeps them correct: every registered
# function must have a description, and every description must name a
# registered function. This prevents a difference between the reference and
# the binding.
#
# Three inputs:
#
#   userfunctions.c           The AddUDF calls. They decide which functions
#                             exist, and in which order. Two annotations go
#                             with them:
#
#                               /* doc-group: <title> */    starts a group.
#                                   The title matches a "## <title>"
#                                   heading in the template.
#                               /* doc: <args> -> <kind> */ follows one AddUDF.
#                                   It gives the argument names in call
#                                   order, "?" for an optional argument,
#                                   and the kind of value that the handler
#                                   returns.
#
#   docs/api-template.md      Everything that is not a function entry. Each
#                             "<!-- functions -->" marker is replaced with
#                             the entries of its group.
#
#   docs/api-descriptions.md  One "### <function name>" block for each
#                             function, with the text for it.
#
# Usage:
#   docs/gen-api-docs.sh [output-file]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 1

out=${1:-$root/API.md}
source=userfunctions.c
template=docs/api-template.md
descriptions=docs/api-descriptions.md

for f in "$source" "$template" "$descriptions"; do
	[ -f "$f" ] || { echo "gen-api-docs: no $f" >&2; exit 1; }
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

version=$(sed -n 's/^SVN_VERSION *?*= *\([0-9.]*\).*/\1/p' makefile | head -1)
bound=$(grep -c 'AddUDF(env,"svn-' "$source")

# ----------------------------------------------------------------------
# The registered functions, in registration order: group, name, arguments,
# return
# ----------------------------------------------------------------------
awk '
	/\/\* doc-group: / {
		line = $0
		sub(/^.*\/\* doc-group: /, "", line)
		sub(/ \*\/.*$/, "", line)
		group = line
		next
	}
	/AddUDF\(env,"svn-/ {
		line = $0

		name = line
		sub(/^.*AddUDF\(env,"/, "", name)
		sub(/".*$/, "", name)

		# The return-type restriction string. It shows if the function
		# can return FALSE for a rejected call.
		ret = line
		sub(/^.*AddUDF\(env,"[^"]*","/, "", ret)
		sub(/".*$/, "", ret)

		# The minimum and maximum argument counts. They show which
		# arguments are optional.
		counts = line
		sub(/^.*AddUDF\(env,"[^"]*","[^"]*",/, "", counts)
		split(counts, c, ",")

		doc = ""
		if (line ~ /\/\* doc: /) {
			doc = line
			sub(/^.*\/\* doc: /, "", doc)
			sub(/ \*\/.*$/, "", doc)
		}

		printf "%s\t%s\t%s\t%s\t%s\t%s\n", group, name, c[1], c[2], doc, ret
	}
' "$source" > "$tmp/registered"

[ -s "$tmp/registered" ] || { echo "gen-api-docs: no registrations found" >&2; exit 1; }

# Every registration must have a doc annotation. A new function without one
# must not pass without a report.
missing=$(awk -F'\t' '$5 == "" { print "    " $2 }' "$tmp/registered")
if [ -n "$missing" ]; then
	echo "gen-api-docs: these registrations have no /* doc: ... */ annotation:" >&2
	echo "$missing" >&2
	exit 1
fi

# ----------------------------------------------------------------------
# The descriptions, split into one file for each function
# ----------------------------------------------------------------------
mkdir -p "$tmp/desc"
awk -v dir="$tmp/desc" '
	/^### / {
		name = $2
		file = dir "/" name
		printf "" > file
		next
	}
	name != "" { print >> file }
' "$descriptions"

status=0

# Every function needs a description, and every description needs a function.
while IFS="$(printf '\t')" read -r group name low high doc ret; do
	if [ ! -f "$tmp/desc/$name" ]; then
		echo "gen-api-docs: $name is registered but has no entry in $descriptions" >&2
		status=1
	fi
done < "$tmp/registered"

for file in "$tmp/desc"/*; do
	[ -e "$file" ] || continue
	name=$(basename "$file")
	if ! awk -F'\t' -v n="$name" '$2 == n { found = 1 } END { exit !found }' "$tmp/registered"; then
		echo "gen-api-docs: $descriptions describes $name, which is not registered" >&2
		status=1
	fi
done

[ "$status" -eq 0 ] || exit "$status"

# ----------------------------------------------------------------------
# One entry
# ----------------------------------------------------------------------
entry() {
	name=$1; low=$2; high=$3; doc=$4; ret=$5

	# "conn statement? -> boolean" becomes a call form and a Returns line.
	# A "?" at the end marks an optional argument. It is written [?name].
	args=${doc%%->*}
	kind=${doc##*-> }

	form=$(printf '%s' "$args" | awk '{
		out = ""
		for (i = 1; i <= NF; i++) {
			a = $i
			if (a ~ /\?$/) { sub(/\?$/, "", a); out = out " [?" a "]" }
			else            { out = out " ?" a }
		}
		print out
	}')

	printf '### %s\n\n' "$name"
	printf '```clips\n(%s%s)\n```\n\n' "$name" "$form"

	# Remove only the blank lines at the ends. The blank lines inside
	# separate paragraphs.
	awk '{ line[n++] = $0 }
	     END {
	         s = 0;     while (s < n  && line[s] == "") s++
	         e = n - 1; while (e >= s && line[e] == "") e--
	         for (i = s; i <= e; i++) print line[i]
	     }' "$tmp/desc/$name"
	printf '\n'

	if [ "$low" != "$high" ]; then
		printf '**Arguments:** %s to %s.\n\n' "$low" "$high"
	fi

	# A return string that has "b" with another type returns FALSE for a
	# rejected call. A string that is only "b" is a normal boolean. A
	# string with no "b" belongs to one of the few functions that take no
	# arguments. Those cannot get a bad argument, so they cannot reject a
	# call.
	case "$ret" in
		b)    printf '**Returns:** %s.\n\n' "$kind" ;;
		*b*)  printf '**Returns:** %s, or `FALSE` if the call is refused.\n\n' "$kind" ;;
		*)    printf '**Returns:** %s. This one takes no arguments and cannot be refused.\n\n' "$kind" ;;
	esac
}

# ----------------------------------------------------------------------
# The template, with each marker replaced with the entries of its group
# ----------------------------------------------------------------------
: > "$out"
group=

while IFS= read -r line; do
	case "$line" in
		'## '*)
			group=${line#'## '}
			printf '%s\n' "$line" >> "$out"
			;;
		'<!-- functions -->')
			while IFS="$(printf '\t')" read -r g name low high doc ret; do
				[ "$g" = "$group" ] || continue
				entry "$name" "$low" "$high" "$doc" "$ret" >> "$out"
			done < "$tmp/registered"
			;;
		*)
			printf '%s\n' "$line" \
			    | sed -e "s/@SVN_VERSION@/$version/g" -e "s/@COUNT@/$bound/g" >> "$out"
			;;
	esac
done < "$template"

echo "gen-api-docs: $out ($bound functions)"
