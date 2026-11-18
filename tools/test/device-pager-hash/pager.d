/*
 * pager.d: what GPU buffer mappings cost in the device pager and LinuxKPI.
 * Per call: count, total and distribution of time (ns), for the fault path
 * (linux_cdev_pager_populate = handle lookup + the driver's fault) and the
 * mmap path (cdev_pager_allocate, and the list walk vm_pager_object_lookup
 * it calls), plus mmap/munmap and whole faults of the GL client.
 *
 *	dtrace -s pager.d SECONDS EXECNAME -o pager.out
 */
#pragma D option quiet

fbt::linux_cdev_pager_populate:entry { self->pop = timestamp; }
fbt::linux_cdev_pager_populate:return /self->pop/ {
	@n["populate (a GPU-buffer page fault)"] = count();
	@t["populate (a GPU-buffer page fault)"] = sum(timestamp - self->pop);
	@q["populate ns"] = quantize(timestamp - self->pop);
	self->pop = 0;
}
fbt::cdev_pager_allocate:entry { self->alloc = timestamp; }
fbt::cdev_pager_allocate:return /self->alloc/ {
	@n["cdev_pager_allocate (mapping a device object)"] = count();
	@t["cdev_pager_allocate (mapping a device object)"] = sum(timestamp - self->alloc);
	@q["cdev_pager_allocate ns"] = quantize(timestamp - self->alloc);
	self->alloc = 0;
}
fbt::vm_pager_object_lookup:entry { self->look = timestamp; }
fbt::vm_pager_object_lookup:return /self->look/ {
	@n["vm_pager_object_lookup (list walk)"] = count();
	@t["vm_pager_object_lookup (list walk)"] = sum(timestamp - self->look);
	@q["vm_pager_object_lookup ns"] = quantize(timestamp - self->look);
	self->look = 0;
}
syscall::mmap:entry, syscall::munmap:entry /execname == $$2/ { self->sc = timestamp; }
syscall::mmap:return, syscall::munmap:return /self->sc/ {
	@n[strjoin(probefunc, " (GL client)")] = count();
	@t[strjoin(probefunc, " (GL client)")] = sum(timestamp - self->sc);
	self->sc = 0;
}
fbt::vm_fault:entry /execname == $$2/ { self->vf = timestamp; }
fbt::vm_fault:return /self->vf/ {
	@n["vm_fault (GL client, all kinds)"] = count();
	@t["vm_fault (GL client, all kinds)"] = sum(timestamp - self->vf);
	self->vf = 0;
}
tick-1s { secs++; }
tick-1s /secs == $1/ { exit(0); }
END {
	printf("calls:\n"); printa("  %-50s %@12d\n", @n);
	printf("total ns:\n"); printa("  %-50s %@14d\n", @t);
	printa(@q);
}
