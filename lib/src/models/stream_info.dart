import 'torrent_state.dart';

/// Information about an active stream.
class StreamInfo {
  final int id;
  final int torrentId;
  final int fileIndex;
  final String url;
  final int fileSize;
  final int readHead;
  final StreamState streamState;
  final double bufferSeconds;
  final int bufferPieces;
  final int readaheadWindow;
  final int activePeers;
  final int downloadRate;

  const StreamInfo({
    required this.id,
    required this.torrentId,
    required this.fileIndex,
    required this.url,
    required this.fileSize,
    required this.readHead,
    required this.streamState,
    required this.bufferSeconds,
    required this.bufferPieces,
    required this.readaheadWindow,
    required this.activePeers,
    required this.downloadRate,
  });

  bool get isReady => streamState == StreamState.ready;
  bool get isBuffering => streamState == StreamState.buffering;
  bool get isSeeking => streamState == StreamState.seeking;
  bool get isActive => streamState != StreamState.idle && streamState != StreamState.error;

  /// Backward-compatible buffer percentage (0.0–1.0).
  /// Derived from bufferPieces relative to readaheadWindow.
  double get bufferPct => readaheadWindow > 0
      ? (bufferPieces / readaheadWindow).clamp(0.0, 1.0)
      : 0.0;

  @override
  String toString() => 'StreamInfo(id=$id, url=$url, state=$streamState, '
      'buffer=${bufferSeconds.toStringAsFixed(1)}s, peers=$activePeers)';
}
