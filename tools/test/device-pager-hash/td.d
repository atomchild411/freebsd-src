/*
 * td.d SECONDS: during a timedemo, the leaf-only kernel profile (997 Hz;
 * arm64 PAC defeats deeper unwinding) and the exact time and calls of the
 * lookup and the GPU page fault.
 */
#pragma D option quiet
profile-997 /arg0/ { @leaf[func(arg0)] = count(); @all = count(); }
fbt::vm_pager_object_lookup:entry, fbt::linux_cdev_pager_populate:entry { self->ts[probefunc] = timestamp; }
fbt::vm_pager_object_lookup:return, fbt::linux_cdev_pager_populate:return /self->ts[probefunc]/ {
	@t[probefunc] = sum(timestamp - self->ts[probefunc]);
	@n[probefunc] = count();
	self->ts[probefunc] = 0;
}
tick-1s { s++; }
tick-1s /s == $1/ { exit(0); }
END {
	trunc(@leaf, 25);
	printf("=== kernel samples ===\n"); printa("%@d\n", @all);
	printf("=== leaf ===\n"); printa("%a %@d\n", @leaf);
	printf("=== totals (ns) ===\n"); printa("%s %@d\n", @t);
	printf("=== calls ===\n"); printa("%s %@d\n", @n);
}
