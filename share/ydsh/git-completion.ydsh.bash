#!/bin/bash

# ydsh completion support for git
#
# based on (https://github.com/git/git/blob/master/contrib/completion/git-completion.tcsh)

# usage: $0 [full path of git-completion.bash] [git/gitk] [comp line]

# shellcheck disable=SC1090
source "$1"

COMP_WORDBREAKS=${COMP_WORDBREAKS//:}

COMP_WORDS=($3)

if [ "${3: -1}" == " " ]; then
	# The last character is a space, so our location is at the end
	# of the command-line array
	COMP_CWORD=${#COMP_WORDS[@]}
else
	# The last character is not a space, so our location is on the
	# last word of the command-line array, so we must decrement the
	# count by 1
	COMP_CWORD=$((${#COMP_WORDS[@]}-1))
fi

_"$2"

IFS=$'\n'
echo "${COMPREPLY[*]}"


