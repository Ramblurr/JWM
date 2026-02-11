package io.github.humbleui.jwm.examples;

import io.github.humbleui.jwm.*;
import io.github.humbleui.jwm.skija.*;
import io.github.humbleui.skija.*;
import io.github.humbleui.types.*;

import java.util.function.*;

public class Example implements Consumer<Event> {
    public Window _window;
    public int _frameCount = 0;

    public Example() {
        _window = App.makeWindow();
        _window.setEventListener(this);
        _window.setTitle("Raster");
        if (_window instanceof WindowWayland windowWayland) {
            windowWayland.setAppId("jwm-example-raster");
        }
        _window.setLayer(new LayerRasterSkija());

        Screen screen = App.getPrimaryScreen();
        float scale = screen.getScale();
        IRect bounds = screen.getWorkArea();

        _window.setWindowSize((int) (720 * scale), (int) (480 * scale));
        _window.setWindowPosition(bounds.getLeft() + 120, bounds.getTop() + 120);
        _window.setVisible(true);
        _window.requestFrame();
    }

    public void paint(Canvas canvas, int width, int height) {
        float t = (float) (_frameCount % 360) / 360f;
        int r = (int) (0x20 + 0x90 * (0.5f + 0.5f * (float) Math.sin(t * Math.PI * 2)));
        int g = (int) (0x30 + 0x80 * (0.5f + 0.5f * (float) Math.sin((t + 0.33f) * Math.PI * 2)));
        int b = (int) (0x40 + 0x70 * (0.5f + 0.5f * (float) Math.sin((t + 0.66f) * Math.PI * 2)));
        int bg = 0xFF000000 | (r << 16) | (g << 8) | b;

        canvas.clear(bg);

        try (Paint paint = new Paint()) {
            float orbit = (float) Math.sin(t * Math.PI * 2);
            float x = width * 0.5f + orbit * width * 0.25f;
            float y = height * 0.5f;
            paint.setColor(0xFFE9C46A);
            canvas.drawCircle(x, y, Math.min(width, height) * 0.12f, paint);
        }
    }

    @Override
    public void accept(Event event) {
        if (event instanceof EventWindowCloseRequest) {
            _window.close();
            return;
        }
        if (event instanceof EventWindowClose) {
            if (App._windows.size() == 0) {
                App.terminate();
            }
            return;
        }
        if (event instanceof EventFrame) {
            _window.requestFrame();
            return;
        }
        if (event instanceof EventFrameSkija frameSkija) {
            Surface surface = frameSkija.getSurface();
            paint(surface.getCanvas(), surface.getWidth(), surface.getHeight());
            _frameCount++;
        }
    }

    public static void main(String[] args) {
        App.start(() -> new Example());
    }
}
