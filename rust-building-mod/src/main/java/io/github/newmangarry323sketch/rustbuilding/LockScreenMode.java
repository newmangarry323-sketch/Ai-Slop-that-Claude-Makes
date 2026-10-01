package io.github.newmangarry323sketch.rustbuilding;

/** What the code lock screen is for. */
public enum LockScreenMode {
	/** Fitting a new lock: choose its code. */
	SET,
	/** Someone not on the lock's list: type the code to get in. */
	ENTER,
	/** The owner: change the code or take the lock off. */
	OWNER
}
