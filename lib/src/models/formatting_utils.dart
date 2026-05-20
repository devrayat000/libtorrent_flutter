import 'torrent_info.dart';

/// Format [bytes] as a human-readable string (e.g. "1.5 GB").
String formatBytes(int bytes, {int decimals = 1}) {
  if (bytes <= 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  int i = 0;
  double v = bytes.toDouble();
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i++;
  }
  return '${v.toStringAsFixed(decimals)} ${units[i]}';
}

/// Format bytes-per-second as a speed string.
String formatSpeed(int bps) => '${formatBytes(bps)}/s';

/// Format estimated time remaining for a torrent.
String formatEta(TorrentInfo t) {
  if (t.downloadRate <= 0) return '∞';
  final remaining = t.totalWanted - t.totalDone;
  if (remaining <= 0) return 'Done';
  final secs = remaining ~/ t.downloadRate;
  if (secs < 60) return '${secs}s';
  if (secs < 3600) return '${secs ~/ 60}m ${secs % 60}s';
  return '${secs ~/ 3600}h ${(secs % 3600) ~/ 60}m';
}
