import 'ffi_bindings.dart';
import 'models.dart';

/// Convert native FFI [LtTorrentStatus] to Dart [TorrentInfo].
TorrentInfo toTorrentInfo(LtTorrentStatus s) => TorrentInfo(
  id:            s.id,
  name:          readCharArray(s.name, 512),
  savePath:      readCharArray(s.savePath, 1024),
  errorMsg:      readCharArray(s.errorMsg, 256),
  state:         stateFromInt(s.state),
  progress:      s.progress.clamp(0.0, 1.0),
  downloadRate:  s.downloadRate,
  uploadRate:    s.uploadRate,
  totalDone:     s.totalDone,
  totalWanted:   s.totalWanted,
  totalUploaded: s.totalUploaded,
  numPeers:      s.numPeers,
  numSeeds:      s.numSeeds,
  isPaused:      s.isPaused != 0,
  isFinished:    s.isFinished != 0,
  hasMetadata:   s.hasMetadata != 0,
  queuePosition: s.queuePosition,
  downloadLimit: s.downloadLimit,
  uploadLimit:   s.uploadLimit,
  isAutoManaged: s.isAutoManaged != 0,
);

/// Convert native FFI [LtFileInfo] to Dart [FileInfo].
FileInfo toFileInfo(LtFileInfo f) => FileInfo(
  index:        f.index,
  name:         readCharArray(f.name, 512),
  path:         readCharArray(f.path, 1024),
  size:         f.size,
  isStreamable: f.isStreamable != 0,
  priority:     f.priority,
);

/// Convert native FFI [LtStreamStatus] to Dart [StreamInfo].
StreamInfo toStreamInfo(LtStreamStatus s) => StreamInfo(
  id:              s.id,
  torrentId:       s.torrentId,
  fileIndex:       s.fileIndex,
  url:             readCharArray(s.url, 256),
  fileSize:        s.fileSize,
  readHead:        s.readHead,
  streamState:     streamStateFromInt(s.streamState),
  bufferSeconds:   s.bufferSeconds,
  bufferPieces:    s.bufferPieces,
  readaheadWindow: s.readaheadWindow,
  activePeers:     s.activePeers,
  downloadRate:    s.downloadRate,
);
