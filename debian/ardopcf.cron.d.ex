#
# Regular cron jobs for the ardopcf package.
#
0 4	* * *	root	[ -x /usr/bin/ardopcf_maintenance ] && /usr/bin/ardopcf_maintenance
