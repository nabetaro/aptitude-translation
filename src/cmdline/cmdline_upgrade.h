// cmdline_upgrade.h                        -*-c++-*-
//
//  Copyright 2004 Daniel Burrows

#ifndef CMDLINE_UPGRADE_H
#define CMDLINE_UPGRADE_H

#include "cmdline_util.h"

#include <vector>

int cmdline_upgrade(int argc, char *argv[],
		    const char *status_fname, bool simulate,
		    bool no_new_installs,
		    bool assume_yes, bool download_only,
		    bool showvers, bool showdeps, bool showsize,
		    const std::vector<aptitude::cmdline::tag_application> &user_tags,
		    bool visual_preview, bool always_prompt,
		    bool queue_only, int verbose);

#endif // CMDLINE_UPGRADE_H
