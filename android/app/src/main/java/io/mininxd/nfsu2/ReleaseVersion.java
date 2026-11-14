package io.mininxd.nfsu2;

/** Compare release tags numerically and semantically, handling 3/4-part numbers and pre-releases. */
final class ReleaseVersion {
    static final class ParsedVersion {
        final int[] numbers;
        final String prerelease;

        ParsedVersion(int[] numbers, String prerelease) {
            this.numbers = numbers;
            this.prerelease = prerelease;
        }
    }

    static ParsedVersion parse(String version) {
        if (version == null) return null;
        String s = version.trim();
        if (s.startsWith("v") || s.startsWith("V")) s = s.substring(1);
        if (s.isEmpty()) return null;

        String prerelease = null;
        int dash = s.indexOf('-');
        if (dash < 0) dash = s.indexOf('_');
        if (dash >= 0) {
            prerelease = s.substring(dash + 1).trim();
            s = s.substring(0, dash).trim();
        } else {
            int firstLetter = -1;
            for (int i = 0; i < s.length(); i++) {
                char c = s.charAt(i);
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                    firstLetter = i;
                    break;
                }
            }
            if (firstLetter > 0) {
                prerelease = s.substring(firstLetter).trim();
                s = s.substring(0, firstLetter).trim();
                if (s.endsWith(".")) s = s.substring(0, s.length() - 1).trim();
            }
        }

        String[] parts = s.split("\\.");
        if (parts.length == 0) return null;
        int[] nums = new int[Math.max(parts.length, 4)];
        for (int i = 0; i < parts.length; i++) {
            try {
                nums[i] = Integer.parseInt(parts[i].trim());
            } catch (Exception e) {
                return null;
            }
        }
        return new ParsedVersion(nums, prerelease);
    }

    static boolean newer(String remote, String installed) {
        ParsedVersion a = parse(remote);
        ParsedVersion b = parse(installed);
        if (a == null || b == null) {
            return remote != null && !remote.equalsIgnoreCase(installed);
        }

        int maxLen = Math.max(a.numbers.length, b.numbers.length);
        for (int i = 0; i < maxLen; i++) {
            int numA = i < a.numbers.length ? a.numbers[i] : 0;
            int numB = i < b.numbers.length ? b.numbers[i] : 0;
            if (numA != numB) {
                return numA > numB;
            }
        }

        // Numeric segments are identical (e.g. 0.15.24 vs 0.15.24).
        // SemVer: A final release (without prerelease) is newer than a pre-release.
        if (a.prerelease == null && b.prerelease != null) {
            return true;
        }
        if (a.prerelease != null && b.prerelease == null) {
            return false;
        }
        if (a.prerelease != null && b.prerelease != null) {
            return comparePrerelease(a.prerelease, b.prerelease) > 0;
        }

        return false;
    }

    private static int comparePrerelease(String a, String b) {
        if (a == null && b == null) return 0;
        if (a == null) return 1;
        if (b == null) return -1;
        String[] partsA = a.split("[.\\-_]");
        String[] partsB = b.split("[.\\-_]");
        int len = Math.max(partsA.length, partsB.length);
        for (int i = 0; i < len; i++) {
            if (i >= partsA.length) return -1;
            if (i >= partsB.length) return 1;
            String pa = partsA[i];
            String pb = partsB[i];
            boolean aIsNum = pa.matches("\\d+");
            boolean bIsNum = pb.matches("\\d+");
            if (aIsNum && bIsNum) {
                try {
                    int na = Integer.parseInt(pa);
                    int nb = Integer.parseInt(pb);
                    if (na != nb) return Integer.compare(na, nb);
                } catch (Exception ignored) {
                    int cmp = pa.compareToIgnoreCase(pb);
                    if (cmp != 0) return cmp;
                }
            } else {
                int cmp = pa.compareToIgnoreCase(pb);
                if (cmp != 0) return cmp;
            }
        }
        return 0;
    }
}
