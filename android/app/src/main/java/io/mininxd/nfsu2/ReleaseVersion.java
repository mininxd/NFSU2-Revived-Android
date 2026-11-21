package io.mininxd.nfsu2;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Compares release version tags numerically and accounts for pre-release / beta semantics.
 * Examples:
 *   v0.15.24 > 0.15.23.3
 *   0.15.24.1 > 0.15.24
 *   v0.15.25.beta1 > 0.15.24.1
 *   0.15.24 > 0.15.24.beta1
 */
final class ReleaseVersion {
    private static volatile boolean nativeLoaded = false;
    static {
        try {
            System.loadLibrary("game_core");
            nativeLoaded = true;
        } catch (Throwable ignored) {}
    }

    private static native boolean nativeSameVersion(String remote, String installed);
    private static native boolean nativeNewer(String remote, String installed);
    private static native boolean nativeIsUpdateAvailable(String remoteTag, String installedVersion, boolean isBetaChannel, boolean isPrerelease);

    private static final Pattern VERSION_PATTERN =
            Pattern.compile("^v?([0-9]+(?:\\.[0-9]+)*)(?:[-._]?(beta|alpha|rc|pre)([0-9]*))?$", Pattern.CASE_INSENSITIVE);

    private static final class Parsed {
        final List<Integer> numbers;
        final int preLevel; // 0=alpha, 1=beta/pre, 2=rc, 999=final release
        final int preNumber;

        Parsed(List<Integer> numbers, int preLevel, int preNumber) {
            this.numbers = numbers;
            this.preLevel = preLevel;
            this.preNumber = preNumber;
        }
    }

    private static Parsed parse(String version) {
        if (version == null || version.trim().isEmpty()) return null;
        String s = version.trim();
        Matcher m = VERSION_PATTERN.matcher(s);
        if (!m.matches()) {
            // Fallback: extract all contiguous numbers
            Matcher numMatcher = Pattern.compile("[0-9]+").matcher(s);
            List<Integer> nums = new ArrayList<>();
            while (numMatcher.find()) {
                try { nums.add(Integer.parseInt(numMatcher.group())); }
                catch (NumberFormatException ignored) {}
            }
            if (nums.isEmpty()) return null;
            return new Parsed(nums, 999, 0);
        }
        String[] parts = m.group(1).split("\\.");
        List<Integer> numbers = new ArrayList<>();
        for (String p : parts) {
            try { numbers.add(Integer.parseInt(p)); }
            catch (NumberFormatException ignored) {}
        }
        String preType = m.group(2);
        int preLevel = 999;
        int preNumber = 0;
        if (preType != null) {
            String lower = preType.toLowerCase(Locale.ROOT);
            if ("alpha".equals(lower)) preLevel = 0;
            else if ("beta".equals(lower) || "pre".equals(lower)) preLevel = 1;
            else if ("rc".equals(lower)) preLevel = 2;

            String pNumStr = m.group(3);
            if (pNumStr != null && !pNumStr.isEmpty()) {
                try { preNumber = Integer.parseInt(pNumStr); }
                catch (NumberFormatException ignored) {}
            }
        }
        return new Parsed(numbers, preLevel, preNumber);
    }

    static boolean sameVersion(String remote, String installed) {
        if (nativeLoaded) {
            try {
                return nativeSameVersion(remote, installed);
            } catch (Throwable ignored) {}
        }
        if (remote == null || installed == null) return false;
        String sRemote = remote.trim().replaceFirst("(?i)^v", "");
        String sInstalled = installed.trim().replaceFirst("(?i)^v", "");
        if (sRemote.equalsIgnoreCase(sInstalled)) return true;

        Parsed a = parse(remote);
        Parsed b = parse(installed);
        if (a == null || b == null) return false;

        if (a.preLevel != b.preLevel || a.preNumber != b.preNumber) {
            return false;
        }

        int maxLen = Math.max(a.numbers.size(), b.numbers.size());
        for (int i = 0; i < maxLen; i++) {
            int na = i < a.numbers.size() ? a.numbers.get(i) : 0;
            int nb = i < b.numbers.size() ? b.numbers.get(i) : 0;
            if (na != nb) return false;
        }
        return true;
    }

    static boolean isUpdateAvailable(String remoteTag, String installedVersion, boolean isBetaChannel, boolean isPrerelease) {
        if (nativeLoaded) {
            try {
                return nativeIsUpdateAvailable(remoteTag, installedVersion, isBetaChannel, isPrerelease);
            } catch (Throwable ignored) {}
        }
        if (remoteTag == null || remoteTag.trim().isEmpty()) return false;
        if (sameVersion(remoteTag, installedVersion)) return false;

        // If not on beta channel, ignore pre-releases
        if (!isBetaChannel && isPrerelease) {
            return false;
        }

        // Only offer an update if the remote version is strictly newer than installed
        return newer(remoteTag, installedVersion);
    }

    static boolean newer(String remote, String installed) {
        if (nativeLoaded) {
            try {
                return nativeNewer(remote, installed);
            } catch (Throwable ignored) {}
        }
        Parsed a = parse(remote);
        Parsed b = parse(installed);
        if (a == null || b == null) return false;

        int maxLen = Math.max(a.numbers.size(), b.numbers.size());
        for (int i = 0; i < maxLen; i++) {
            int na = i < a.numbers.size() ? a.numbers.get(i) : 0;
            int nb = i < b.numbers.size() ? b.numbers.get(i) : 0;
            if (na != nb) return na > nb;
        }
        if (a.preLevel != b.preLevel) {
            return a.preLevel > b.preLevel;
        }
        return a.preNumber > b.preNumber;
    }
}
