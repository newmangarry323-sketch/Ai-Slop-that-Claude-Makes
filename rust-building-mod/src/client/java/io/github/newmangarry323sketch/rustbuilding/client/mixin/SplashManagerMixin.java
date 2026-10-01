package io.github.newmangarry323sketch.rustbuilding.client.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import net.minecraft.client.gui.components.SplashRenderer;
import net.minecraft.client.resources.SplashManager;

import io.github.newmangarry323sketch.rustbuilding.client.RustcraftSplash;

/**
 * The title screen's splash text is always RUSTcraft. Minecraft normally picks one at random from
 * texts/splashes.txt, except on Christmas Eve, New Year's Day and Halloween, when it uses a fixed one;
 * answering before any of that covers every day.
 */
@Mixin(SplashManager.class)
public abstract class SplashManagerMixin {
	@Inject(method = "getSplash", at = @At("HEAD"), cancellable = true)
	private void rustbuilding$rustcraft(CallbackInfoReturnable<SplashRenderer> callback) {
		callback.setReturnValue(RustcraftSplash.SPLASH);
	}
}
