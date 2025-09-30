using System;

namespace ProjectMino.Client
{
    // Encapsulates scoring and judgment logic for the map.
    public class ScoreStat
    {
        // Basic score fields
        public int Score { get; private set; }
        public int Combo { get; private set; }
        public int HighestCombo { get; private set; }
        public int Misses { get; private set; }

        // Judgment categories (string labels used in UI)
        public enum JudgmentKind { Perfect, Good, Meh, Miss }

        // Reset state
        public void Reset()
        {
            Score = 0;
            Combo = 0;
            HighestCombo = 0;
            Misses = 0;
        }

        // Register a hit; returns the judgment kind determined by timing delta
        // scheduledMs: the expected hit time in ms (map time)
        // actualMs: the actual hit time in ms (engine elapsed)
        // scoreForNote: base score for a note
        public JudgmentKind RegisterHitWithTiming(int scheduledMs, int actualMs, int scoreForNote = 100)
        {
            int delta = Math.Abs(actualMs - scheduledMs);
            JudgmentKind kind;
            // thresholds in milliseconds
            if (delta <= 60) kind = JudgmentKind.Perfect;
            else if (delta <= 120) kind = JudgmentKind.Good;
            else if (delta <= 200) kind = JudgmentKind.Meh;
            else kind = JudgmentKind.Good; // fallback to Good for late but collected

            Combo++;
            if (Combo > HighestCombo) HighestCombo = Combo;
            Score += scoreForNote * Math.Max(1, Combo);

            return kind;
        }

        // Register a miss
        public JudgmentKind RegisterMiss()
        {
            Misses++;
            Combo = 0;
            return JudgmentKind.Miss;
        }
    }
}
