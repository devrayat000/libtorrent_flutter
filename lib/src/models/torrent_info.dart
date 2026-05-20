import 'torrent_state.dart';

/// Information about a torrent.
class TorrentInfo {
  final int id;
  final String name;
  final String savePath;
  final String errorMsg;
  final TorrentState state;
  final double progress;
  final int downloadRate;
  final int uploadRate;
  final int totalDone;
  final int totalWanted;
  final int totalUploaded;
  final int numPeers;
  final int numSeeds;
  final bool isPaused;
  final bool isFinished;
  final bool hasMetadata;
  final int queuePosition;
  
  /// Download rate limit for this torrent in bytes/s (0 = unlimited).
  final int downloadLimit;

  /// Upload rate limit for this torrent in bytes/s (0 = unlimited).
  final int uploadLimit;

  /// Whether this torrent is auto-managed by the session queue.
  final bool isAutoManaged;

  const TorrentInfo({
    required this.id,
    required this.name,
    required this.savePath,
    required this.errorMsg,
    required this.state,
    required this.progress,
    required this.downloadRate,
    required this.uploadRate,
    required this.totalDone,
    required this.totalWanted,
    required this.totalUploaded,
    required this.numPeers,
    required this.numSeeds,
    required this.isPaused,
    required this.isFinished,
    required this.hasMetadata,
    required this.queuePosition,
    this.downloadLimit = 0,
    this.uploadLimit = 0,
    this.isAutoManaged = true,
  });

  TorrentInfo copyWith({
    String? name,
    String? savePath,
    String? errorMsg,
    TorrentState? state,
    double? progress,
    int? downloadRate,
    int? uploadRate,
    int? totalDone,
    int? totalWanted,
    int? totalUploaded,
    int? numPeers,
    int? numSeeds,
    bool? isPaused,
    bool? isFinished,
    bool? hasMetadata,
    int? queuePosition,
    int? downloadLimit,
    int? uploadLimit,
    bool? isAutoManaged,
  }) => TorrentInfo(
    id: id,
    name: name ?? this.name,
    savePath: savePath ?? this.savePath,
    errorMsg: errorMsg ?? this.errorMsg,
    state: state ?? this.state,
    progress: progress ?? this.progress,
    downloadRate: downloadRate ?? this.downloadRate,
    uploadRate: uploadRate ?? this.uploadRate,
    totalDone: totalDone ?? this.totalDone,
    totalWanted: totalWanted ?? this.totalWanted,
    totalUploaded: totalUploaded ?? this.totalUploaded,
    numPeers: numPeers ?? this.numPeers,
    numSeeds: numSeeds ?? this.numSeeds,
    isPaused: isPaused ?? this.isPaused,
    isFinished: isFinished ?? this.isFinished,
    hasMetadata: hasMetadata ?? this.hasMetadata,
    queuePosition: queuePosition ?? this.queuePosition,
    downloadLimit: downloadLimit ?? this.downloadLimit,
    uploadLimit: uploadLimit ?? this.uploadLimit,
    isAutoManaged: isAutoManaged ?? this.isAutoManaged,
  );

  @override
  String toString() => 'TorrentInfo(id=$id, name=$name, state=$state, '
      'progress=${(progress * 100).toStringAsFixed(1)}%, '
      'dlLimit=$downloadLimit, ulLimit=$uploadLimit, autoManaged=$isAutoManaged)';
}
