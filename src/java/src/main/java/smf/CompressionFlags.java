// automatically generated by the FlatBuffers compiler, do not modify

package smf;

/**
 * \brief: headers that are stored in an int
 * so they need to be inclusive. That is, you can turn on
 * many flags at the same time, i.e.: enable checksum and
 * have the payload be zlib compressed.
 *
 */
public final class CompressionFlags {
  private CompressionFlags() { }
  public static final byte None = 0;
  public static final byte Disabled = 1;
  /**
   * brief uses zstandard 1.0
   */
  public static final byte Zstd = 2;
  /**
   * lz4 is pending
   */
  public static final byte Lz4 = 3;

  public static final String[] names = { "None", "Disabled", "Zstd", "Lz4", };

  public static String name(int e) { return names[e]; }
}

