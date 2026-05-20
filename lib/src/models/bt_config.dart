/// TorrServer-equivalent engine configuration.
///
/// Port of TorrServer's `settings.BTSets` struct. Controls cache, connections,
/// encryption, protocol toggles, rate limits, etc.
class BtConfig {
  /// Cache size in bytes (default 64 MB).
  final int cacheSize;

  /// Percentage of cache used for read-ahead (5–100, default 95).
  final int readerReadAhead;

  /// Percentage of cache preloaded on stream start (0–100, default 50).
  final int preloadCache;

  /// Max concurrent piece requests per reader (default 25).
  final int connectionsLimit;

  /// Seconds of inactivity before a reader-less torrent is paused (default 30).
  final int torrentDisconnectTimeout;

  /// Force encrypted connections only.
  final bool forceEncrypt;

  /// Disable TCP transport (UTP only).
  final bool disableTcp;

  /// Disable UTP transport (TCP only).
  final bool disableUtp;

  /// Disable uploading to other peers.
  final bool disableUpload;

  /// Disable DHT peer discovery.
  final bool disableDht;

  /// Disable UPnP / NAT-PMP port forwarding.
  final bool disableUpnp;

  /// Enable IPv6 listening.
  final bool enableIpv6;

  /// Download rate limit in KB/s (0 = unlimited).
  final int downloadRateLimit;

  /// Upload rate limit in KB/s (0 = unlimited).
  final int uploadRateLimit;

  /// Port for incoming peer connections (0 = default).
  final int peersListenPort;

  /// Max active downloading torrents (0 = no limit).
  final int activeDownloadsLimit;

  /// Max active seeding torrents (0 = no limit).
  final int activeSeedsLimit;

  /// Max active torrents overall (0 = no limit).
  final int activeLimit;

  /// Enable responsive mode for readers (lower latency, more aggressive).
  final bool responsiveMode;

  const BtConfig({
    this.cacheSize = 64 * 1024 * 1024,
    this.readerReadAhead = 95,
    this.preloadCache = 50,
    this.connectionsLimit = 25,
    this.torrentDisconnectTimeout = 30,
    this.forceEncrypt = false,
    this.disableTcp = false,
    this.disableUtp = false,
    this.disableUpload = false,
    this.disableDht = false,
    this.disableUpnp = false,
    this.enableIpv6 = false,
    this.downloadRateLimit = 0,
    this.uploadRateLimit = 0,
    this.peersListenPort = 0,
    this.activeDownloadsLimit = 0,
    this.activeSeedsLimit = 0,
    this.activeLimit = 0,
    this.responsiveMode = true,
  });

  BtConfig copyWith({
    int? cacheSize,
    int? readerReadAhead,
    int? preloadCache,
    int? connectionsLimit,
    int? torrentDisconnectTimeout,
    bool? forceEncrypt,
    bool? disableTcp,
    bool? disableUtp,
    bool? disableUpload,
    bool? disableDht,
    bool? disableUpnp,
    bool? enableIpv6,
    int? downloadRateLimit,
    int? uploadRateLimit,
    int? peersListenPort,
    int? activeDownloadsLimit,
    int? activeSeedsLimit,
    int? activeLimit,
    bool? responsiveMode,
  }) => BtConfig(
    cacheSize: cacheSize ?? this.cacheSize,
    readerReadAhead: readerReadAhead ?? this.readerReadAhead,
    preloadCache: preloadCache ?? this.preloadCache,
    connectionsLimit: connectionsLimit ?? this.connectionsLimit,
    torrentDisconnectTimeout: torrentDisconnectTimeout ?? this.torrentDisconnectTimeout,
    forceEncrypt: forceEncrypt ?? this.forceEncrypt,
    disableTcp: disableTcp ?? this.disableTcp,
    disableUtp: disableUtp ?? this.disableUtp,
    disableUpload: disableUpload ?? this.disableUpload,
    disableDht: disableDht ?? this.disableDht,
    disableUpnp: disableUpnp ?? this.disableUpnp,
    enableIpv6: enableIpv6 ?? this.enableIpv6,
    downloadRateLimit: downloadRateLimit ?? this.downloadRateLimit,
    uploadRateLimit: uploadRateLimit ?? this.uploadRateLimit,
    peersListenPort: peersListenPort ?? this.peersListenPort,
    activeDownloadsLimit: activeDownloadsLimit ?? this.activeDownloadsLimit,
    activeSeedsLimit: activeSeedsLimit ?? this.activeSeedsLimit,
    activeLimit: activeLimit ?? this.activeLimit,
    responsiveMode: responsiveMode ?? this.responsiveMode,
  );

  @override
  String toString() => 'BtConfig(cache=${cacheSize ~/ (1024 * 1024)}MB, '
      'readAhead=$readerReadAhead%, conns=$connectionsLimit, '
      'activeDownloadsLimit=$activeDownloadsLimit, activeSeedsLimit=$activeSeedsLimit, activeLimit=$activeLimit, '
      'encrypt=$forceEncrypt, responsive=$responsiveMode)';
}
