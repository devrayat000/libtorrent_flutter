/// Information about a file within a torrent.
class FileInfo {
  final int index;
  final String name;
  final String path;
  final int size;
  final bool isStreamable;

  /// File priority (0–7). 0 = do not download, 1 = lowest, 4 = default/normal, 7 = highest.
  final int priority;

  const FileInfo({
    required this.index,
    required this.name,
    required this.path,
    required this.size,
    required this.isStreamable,
    this.priority = 4,
  });

  @override
  String toString() => 'FileInfo(index=$index, name=$name, '
      'size=$size, streamable=$isStreamable, priority=$priority)';
}
