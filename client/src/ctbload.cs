using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.Json;

namespace ProjectMino.Client
{
	// Metadata model matching map.json
	public class MapMetadata
	{
		public string? MapId { get; set; }
		public string? Title { get; set; }
		public string? Artist { get; set; }
		public string? Creator { get; set; }
		public string? Background { get; set; }
		public string? SongFile { get; set; }
		public string[]? Tags { get; set; }
		public DateTime? CreatedAt { get; set; }
		public DateTime? UpdatedAt { get; set; }
		public DateTime? LastPlayedAt { get; set; }
	}

	public static class CtbLoader
	{
		// Load map metadata from a map.json file.
		public static MapMetadata? LoadMetadata(string mapJsonPath)
		{
			if (!File.Exists(mapJsonPath)) return null;
			var json = File.ReadAllText(mapJsonPath);
			var opts = new JsonSerializerOptions
			{
				PropertyNameCaseInsensitive = true
			};
			return JsonSerializer.Deserialize<MapMetadata>(json, opts);
		}

		// Parse a .ppmm file. The format we expect is simple lines: time_ms,x,y[,type]
		// Example: 1500,120,30
		public static List<NoteEvent> LoadPpmm(string ppmmPath)
		{
			var list = new List<NoteEvent>();
			if (!File.Exists(ppmmPath)) return list;
			foreach (var line in File.ReadAllLines(ppmmPath))
			{
				var l = line.Trim();
				if (string.IsNullOrEmpty(l) || l.StartsWith("#")) continue;
				var parts = l.Split(',');
				if (parts.Length < 3) continue;
				if (!int.TryParse(parts[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out var time)) continue;
				if (!float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var x)) continue;
				if (!float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out var y)) continue;
				var ev = new NoteEvent { TimeMs = time, X = x, Y = y };
				// parse optional type and/or color information
				if (parts.Length >= 4)
				{
					var p3 = parts[3].Trim();
					// try parse hex color (with or without '#', 6 or 8 digits)
					if (TryParseHexColor(p3, out var hexArgb))
					{
						ev.ColorArgb = hexArgb;
					}
					else if (int.TryParse(p3, NumberStyles.Integer, CultureInfo.InvariantCulture, out var t))
					{
						ev.Type = t;
					}
				}

				// If color provided as separate numeric parts (e.g., time,x,y,r,g,b) or (time,x,y,type,r,g,b)
				if (parts.Length >= 6)
				{
					// Case A: time,x,y,r,g,b  -> parts[3]=r
					if (int.TryParse(parts[3], out var r1) && int.TryParse(parts[4], out var g1) && int.TryParse(parts[5], out var b1))
					{
						ev.ColorArgb = System.Drawing.Color.FromArgb(255, r1, g1, b1).ToArgb();
					}
					// Case B: time,x,y,type,r,g,b -> parts[3]=type, parts[4..6]=r,g,b
					else if (parts.Length >= 7 && int.TryParse(parts[3], out var maybeType) && int.TryParse(parts[4], out var r2) && int.TryParse(parts[5], out var g2) && int.TryParse(parts[6], out var b2))
					{
						ev.Type = maybeType;
						ev.ColorArgb = System.Drawing.Color.FromArgb(255, r2, g2, b2).ToArgb();
					}
				}
				list.Add(ev);
			}
			return list;
		}

		// Convenience: load metadata and ppmm into an engine
		public static bool LoadMapIntoEngine(string mapFolderPath, CtbEngine engine)
		{
			var metaPath = Path.Combine(mapFolderPath, "map.json");
			var meta = LoadMetadata(metaPath);
			if (meta == null) return false;

			var ppmmPath = Path.Combine(mapFolderPath, meta.MapId + ".ppmm");
			if (!File.Exists(ppmmPath))
			{
				// Try generic map.ppmm
				var alt = Path.Combine(mapFolderPath, "map.ppmm");
				if (File.Exists(alt)) ppmmPath = alt;
			}

			var notes = LoadPpmm(ppmmPath);
			engine.LoadNotes(notes);
			return true;
		}

		// Try parse hex color strings like #RRGGBB, RRGGBB, #AARRGGBB, AARRGGBB
		private static bool TryParseHexColor(string s, out int argb)
		{
			argb = 0;
			if (string.IsNullOrWhiteSpace(s)) return false;
			s = s.Trim();
			if (s.StartsWith("#")) s = s.Substring(1);
			if (s.Length == 6)
			{
				// RRGGBB -> assume fully opaque
				if (int.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var rgb))
				{
					argb = unchecked((int)(0xFF000000u | (uint)rgb));
					return true;
				}
			}
			else if (s.Length == 8)
			{
				// AARRGGBB provided
				if (int.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var aarrggbb))
				{
					argb = unchecked((int)(uint)aarrggbb);
					return true;
				}
			}
			return false;
		}
	}
}

