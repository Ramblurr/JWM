package io.github.humbleui.jwm;

import java.io.File;
import java.net.URL;
import org.jetbrains.annotations.ApiStatus;

public enum Platform {
    WINDOWS,
    WAYLAND,
    X11,
    MACOS;

    @ApiStatus.Internal
    public static boolean _isWaylandSession() {
        String waylandDisplay = System.getenv("WAYLAND_DISPLAY");
        if (waylandDisplay != null && !waylandDisplay.isBlank())
            return true;
        String sessionType = System.getenv("XDG_SESSION_TYPE");
        return sessionType != null && "wayland".equalsIgnoreCase(sessionType);
    }

    @ApiStatus.Internal
    public static boolean _hasWaylandLibrary() {
        URL resource = Platform.class.getResource("/libjwm_wayland_x64.so");
        return resource != null || new File("libjwm_wayland_x64.so").exists();
    }

    public static final Platform CURRENT;
    static {
        String os = System.getProperty("os.name").toLowerCase();
        if (os.contains("mac") || os.contains("darwin"))
            CURRENT = MACOS;
        else if (os.contains("windows"))
            CURRENT = WINDOWS;
        else if (os.contains("nux") || os.contains("nix"))
            CURRENT = _isWaylandSession() && _hasWaylandLibrary() ? WAYLAND : X11;
        else
            throw new RuntimeException("Unsupported platform: " + os);
    }
}
