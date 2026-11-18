/*
 * devobjs.d SECONDS: once a second, the number of device-pager VM objects
 * (OBJT_DEVICE, OBJT_MGTDEVICE) created since tracing began and not yet
 * terminated. Start it before the workload; objects that already existed
 * are not counted.
 *
 *	dtrace -s devobjs.d 180 -o devobjs.out
 */
#pragma D option quiet
#pragma D option dynvarsize=32m

inline int OBJT_DEVICE = 3;
inline int OBJT_MGTDEVICE = 7;

BEGIN
{
	@live = sum(0);
}

fbt::vm_object_allocate:entry
/arg0 == OBJT_DEVICE || arg0 == OBJT_MGTDEVICE/
{
	self->dev = 1;
}

fbt::vm_object_allocate:return
/self->dev/
{
	live[arg1] = 1;
	@live = sum(1);
	self->dev = 0;
}

fbt::vm_object_terminate:entry
/live[arg0]/
{
	live[arg0] = 0;
	@live = sum(-1);
}

tick-1s
{
	printf("%d ", secs++);
	printa("%@d\n", @live);
}

tick-1s
/secs > $1/
{
	exit(0);
}
