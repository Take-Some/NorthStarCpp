// Purpose-built V8 IPC regression fixture: READY followed by permanent
// refusal to read stdin, forcing the native overlapped WriteFile to pend.
print("READY|"+version());
while(true) {}
