/// Torrent download state.
enum TorrentState {
  error,
  unknown,
  checkingFiles,
  downloadingMetadata,
  downloading,
  finished,
  seeding,
  allocating,
  checkingResume;

  bool get isActive =>
      this == TorrentState.downloading ||
      this == TorrentState.downloadingMetadata ||
      this == TorrentState.allocating ||
      this == TorrentState.checkingFiles ||
      this == TorrentState.checkingResume;

  bool get isDone =>
      this == TorrentState.finished || this == TorrentState.seeding;
}

/// Stream playback state.
enum StreamState {
  idle,
  buffering,
  ready,
  seeking,
  error;

  bool get isReady => this == StreamState.ready;
  bool get isBuffering => this == StreamState.buffering;
  bool get isSeeking => this == StreamState.seeking;
  bool get isActive => this != StreamState.idle && this != StreamState.error;
}

StreamState streamStateFromInt(int v) {
  switch (v) {
    case 0: return StreamState.idle;
    case 1: return StreamState.buffering;
    case 2: return StreamState.ready;
    case 3: return StreamState.seeking;
    case 4: return StreamState.error;
    default: return StreamState.idle;
  }
}

/// Convert native integer state to [TorrentState].
TorrentState stateFromInt(int v) {
  switch (v) {
    case -2: return TorrentState.error;
    case  0: return TorrentState.checkingFiles;
    case  1: return TorrentState.downloadingMetadata;
    case  2: return TorrentState.downloading;
    case  3: return TorrentState.finished;
    case  4: return TorrentState.seeding;
    case  5: return TorrentState.allocating;
    case  6: return TorrentState.checkingResume;
    default: return TorrentState.unknown;
  }
}

extension TorrentStateX on TorrentState {
  String get label {
    switch (this) {
      case TorrentState.error:               return 'Error';
      case TorrentState.unknown:             return 'Unknown';
      case TorrentState.checkingFiles:       return 'Checking files';
      case TorrentState.downloadingMetadata: return 'Getting metadata';
      case TorrentState.downloading:         return 'Downloading';
      case TorrentState.finished:            return 'Finished';
      case TorrentState.seeding:             return 'Seeding';
      case TorrentState.allocating:          return 'Allocating';
      case TorrentState.checkingResume:      return 'Checking resume';
    }
  }
}
