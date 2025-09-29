using System;
using System.Collections.Concurrent;
using System.Drawing;

namespace ProjectMino.Client.Assets.Fonts
{
    // Very small font manager: caches Font instances by family/size/style.
    // Returns shared Font instances; do not dispose fonts returned by GetFont.
    public static class FontMan
    {
        private static readonly ConcurrentDictionary<string, Font> cache = new ConcurrentDictionary<string, Font>();

        private static string BuildKey(string family, float size, FontStyle style)
        {
            return string.Concat(family ?? "", "|", size.ToString(System.Globalization.CultureInfo.InvariantCulture), "|", (int)style);
        }

        public static Font GetFont(string family, float size, FontStyle style = FontStyle.Regular)
        {
            if (string.IsNullOrEmpty(family)) family = "Segoe UI";
            var key = BuildKey(family, size, style);
            return cache.GetOrAdd(key, k => new Font(family, size, style, GraphicsUnit.Point));
        }

        // Optional: clear cached fonts (disposes them)
        public static void Clear()
        {
            foreach (var kv in cache)
            {
                try { kv.Value.Dispose(); } catch { }
            }
            cache.Clear();
        }
    }
}
