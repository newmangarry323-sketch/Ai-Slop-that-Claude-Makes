package io.github.newmangarry323sketch.rustbuilding.client;

import net.minecraft.client.gui.components.SplashRenderer;
import net.minecraft.network.chat.Component;
import net.minecraft.network.chat.Style;

/** The title screen's splash text, in the same yellow as Minecraft's own splashes. */
public final class RustcraftSplash {
	public static final SplashRenderer SPLASH = new SplashRenderer(
			Component.literal("RUSTcraft").setStyle(Style.EMPTY.withColor(0xFFFF00)));

	private RustcraftSplash() {
	}
}
