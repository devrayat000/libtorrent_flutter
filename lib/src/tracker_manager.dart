import 'dart:async';
import 'dart:io';

/// Automatically fetches and injects best public trackers into magnet URIs.
class TrackerManager {
  static final List<String> _extraTrackers = [];

  /// Fetch the latest best-performing tracker list from GitHub.
  static Future<void> fetchBestTrackers() async {
    try {
      final client = HttpClient();
      client.connectionTimeout = const Duration(seconds: 5);
      final req = await client.getUrl(Uri.parse(
          'https://raw.githubusercontent.com/ngosang/trackerslist/master/trackers_best.txt'));
      final res = await req.close();
      if (res.statusCode == 200) {
        final body = await res.transform(const SystemEncoding().decoder).join();
        final list = body
            .split('\n')
            .map((s) => s.trim())
            .where((s) => s.isNotEmpty)
            .toList();
        _extraTrackers.clear();
        _extraTrackers.addAll(list);
      }
      client.close(force: true);
    } catch (_) {}
  }

  /// Inject extra trackers into a magnet URI for better peer discovery.
  static String injectTrackers(String magnetUri) {
    if (_extraTrackers.isEmpty) return magnetUri;
    var uri = magnetUri;
    for (final tr in _extraTrackers) {
      if (!uri.contains(Uri.encodeComponent(tr))) {
        uri += '&tr=${Uri.encodeComponent(tr)}';
      }
    }
    return uri;
  }
}
