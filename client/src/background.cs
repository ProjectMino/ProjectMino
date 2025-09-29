using System;
using System.Drawing;
using System.IO;

namespace ProjectMino.Client
{
    // Responsible for loading the map's Background image (from map.json) and
    // drawing it stretched to the renderer backbuffer. Keeps a resized cache
    // to avoid costly per-frame resampling.
    public class Background : IDisposable
    {
        private Image? original;
        private Bitmap? scaledCache;
        private int cacheW = 0;
        private int cacheH = 0;

        // Load the background image referenced in map.json inside the given folder.
        public void LoadFromMapFolder(string mapFolder)
        {
            DisposeOriginal();
            try
            {
                var metaPath = Path.Combine(mapFolder, "map.json");
                var meta = CtbLoader.LoadMetadata(metaPath);
                if (meta?.Background != null)
                {
                    var imgPath = Path.Combine(mapFolder, meta.Background);
                    if (File.Exists(imgPath))
                    {
                        // Image.FromFile keeps the file locked; clone to allow later disposal without keeping lock
                        using (var tmp = Image.FromFile(imgPath))
                        {
                            original = new Bitmap(tmp);
                        }
                    }
                }
            }
            catch
            {
                // ignore errors; background stays null
            }
            // clear any existing scaled cache so it will be regenerated
            DisposeScaledCache();
        }

        public bool HasImage => original != null;

        // Draw the background into the renderer's backbuffer, scaling to fit.
        // If no background is available, this is a no-op.
        public void Draw(Ppmworks.PpmRenderer renderer)
        {
            if (renderer == null) return;
            if (original == null) return;
            var bb = renderer.Backbuffer;
            if (bb == null) return;

            // If size changed or no cache, recreate scaled cache
            if (scaledCache == null || cacheW != renderer.Width || cacheH != renderer.Height)
            {
                DisposeScaledCache();
                try
                {
                    cacheW = Math.Max(1, renderer.Width);
                    cacheH = Math.Max(1, renderer.Height);
                    scaledCache = new Bitmap(cacheW, cacheH, System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
                    using (var g = Graphics.FromImage(scaledCache))
                    {
                        g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                        g.DrawImage(original, 0, 0, cacheW, cacheH);
                    }
                }
                catch
                {
                    DisposeScaledCache();
                    return;
                }
            }

            // Blit the scaled cache into the renderer backbuffer
            try
            {
                using (var g = Graphics.FromImage(bb))
                {
                    g.DrawImageUnscaled(scaledCache, 0, 0);
                    // Apply a semi-transparent black overlay to darken the background by ~45%
                    using (var brush = new SolidBrush(Color.FromArgb(115, 0, 0, 0)))
                    {
                        g.FillRectangle(brush, 0, 0, cacheW, cacheH);
                    }
                }
            }
            catch
            {
                // ignore drawing errors
            }
        }

        private void DisposeOriginal()
        {
            if (original != null) { original.Dispose(); original = null; }
        }

        private void DisposeScaledCache()
        {
            if (scaledCache != null) { scaledCache.Dispose(); scaledCache = null; }
            cacheW = cacheH = 0;
        }

        public void Dispose()
        {
            DisposeOriginal();
            DisposeScaledCache();
        }
    }
}
