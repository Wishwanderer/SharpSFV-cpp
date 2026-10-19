// C# reference oracle: SharpSFV's non-UI logic, copied from the original sources with only
// the WinForms dependencies removed. Part of the SharpSFV C++ port test tooling (GPL-3.0).
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.IO.Hashing;

enum HashType { XXHASH3, Crc32, MD5, SHA1, SHA256 }
enum PathStorageMode { Relative, Absolute }
enum ProcessingMode { Auto, HDD, SSD }

static class Oracle
{
    static int Main(string[] args)
    {
        var o = Console.OpenStandardOutput();
        var w = new StreamWriter(o, new UTF8Encoding(false)) { NewLine = "\n", AutoFlush = true };
        Console.SetOut(w);
        switch (args[0])
        {
            case "hash": return Hash(args);
            case "parse": return Parse(args);
            case "mask": return Mask();
            case "ini": return Ini(args);
            case "create": return Create(args);
            case "verify": return Verify(args);
        }
        return 2;
    }

    // ------------------------------------------------ HashHelper.ComputeHashSync (verbatim logic)
    static byte[]? ComputeHashSync(Stream inputStream, HashType type, byte[] buffer, HashAlgorithm? reusedAlgo)
    {
        int bytesRead;
        try
        {
            switch (type)
            {
                case HashType.XXHASH3:
                    var xx3 = new XxHash128();
                    while ((bytesRead = inputStream.Read(buffer, 0, buffer.Length)) > 0) xx3.Append(new ReadOnlySpan<byte>(buffer, 0, bytesRead));
                    return xx3.GetCurrentHash();
                case HashType.Crc32:
                    var crc = new System.IO.Hashing.Crc32();
                    while ((bytesRead = inputStream.Read(buffer, 0, buffer.Length)) > 0) crc.Append(new ReadOnlySpan<byte>(buffer, 0, bytesRead));
                    return crc.GetCurrentHash();
                case HashType.MD5:
                case HashType.SHA1:
                case HashType.SHA256:
                    if (reusedAlgo == null) return null;
                    while ((bytesRead = inputStream.Read(buffer, 0, buffer.Length)) > 0) reusedAlgo.TransformBlock(buffer, 0, bytesRead, null, 0);
                    reusedAlgo.TransformFinalBlock(Array.Empty<byte>(), 0, 0);
                    return reusedAlgo.Hash;
                default: return null;
            }
        }
        catch { return null; }
    }

    static HashAlgorithm? AlgoFor(HashType t) => t switch
    {
        HashType.MD5 => MD5.Create(),
        HashType.SHA1 => SHA1.Create(),
        HashType.SHA256 => SHA256.Create(),
        _ => null
    };

    static byte[]? HashFile(string path, HashType t)
    {
        try
        {
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            using var a = AlgoFor(t);
            return ComputeHashSync(fs, t, new byte[512 * 1024], a);
        }
        catch { return null; }
    }

    static int Hash(string[] args)
    {
        var t = Enum.Parse<HashType>(args[1], true);
        foreach (var f in args.Skip(2))
        {
            var h = HashFile(f, t);
            Console.WriteLine($"{(h == null ? "ERROR" : Convert.ToHexString(h))}\t{f}");
        }
        return 0;
    }

    // ------------------------------------------------ RunVerification parsing (verbatim)
    static (List<(byte[] ExpectedHash, string Filename)>, string) ParseFile(string checkFilePath, HashType verificationAlgo)
    {
        var parsedLines = new List<(byte[] ExpectedHash, string Filename)>();
        StringBuilder sbComments = new StringBuilder();
        foreach (var line in File.ReadAllLines(checkFilePath))
        {
            string trimmed = line.TrimStart();
            if (trimmed.StartsWith("//"))
            {
                sbComments.AppendLine(trimmed.Substring(2).Trim());
                continue;
            }
            if (string.IsNullOrWhiteSpace(line) || trimmed.StartsWith(";")) continue;

            string expectedHashStr = "", filename = "";
            Match matchA = Regex.Match(line, @"^\s*([0-9a-fA-F]+)\s+\*?(.*?)\s*$");
            Match matchB = Regex.Match(line, @"^\s*(.*?)\s+([0-9a-fA-F]{8})\s*$");

            if (verificationAlgo == HashType.Crc32 && matchB.Success) { filename = matchB.Groups[1].Value.Trim(); expectedHashStr = matchB.Groups[2].Value; }
            else if (matchA.Success) { expectedHashStr = matchA.Groups[1].Value.Trim(); filename = matchA.Groups[2].Value.Trim(); }

            if (!string.IsNullOrEmpty(expectedHashStr))
            {
                try { parsedLines.Add((Convert.FromHexString(expectedHashStr), filename)); } catch { }
            }
        }
        return (parsedLines, sbComments.ToString());
    }

    static HashType DetectAlgo(string checkFilePath, HashType current)
    {
        string ext = Path.GetExtension(checkFilePath).ToLowerInvariant();
        HashType verificationAlgo = current;
        if (ext == ".sfv") verificationAlgo = HashType.Crc32;
        else if (ext == ".md5") verificationAlgo = HashType.MD5;
        else if (ext == ".sha1") verificationAlgo = HashType.SHA1;
        else if (ext == ".sha256") verificationAlgo = HashType.SHA256;
        else if (ext == ".xxh3") verificationAlgo = HashType.XXHASH3;
        return verificationAlgo;
    }

    static int Parse(string[] args)
    {
        var algo = Enum.Parse<HashType>(args[1], true);
        var (lines, comments) = ParseFile(args[2], algo);
        foreach (var (h, f) in lines) Console.WriteLine($"{Convert.ToHexString(h)}|{f}|");
        Console.Write("COMMENTS:" + comments.Replace("\r\n", "\\n").Replace("\n", "\\n") + "\n");
        return 0;
    }

    // ------------------------------------------------ IsFileAllowed / FitsMask (verbatim)
    static bool IsFileAllowed(string filename, string include, string exclude)
    {
        if (!string.IsNullOrWhiteSpace(exclude))
        {
            var patterns = exclude.Split(new[] { ';', ',' }, StringSplitOptions.RemoveEmptyEntries);
            foreach (var p in patterns) if (FitsMask(filename, p.Trim())) return false;
        }
        if (!string.IsNullOrWhiteSpace(include))
        {
            bool match = false;
            var patterns = include.Split(new[] { ';', ',' }, StringSplitOptions.RemoveEmptyEntries);
            if (patterns.Length == 0) return true;
            foreach (var p in patterns) { if (FitsMask(filename, p.Trim())) { match = true; break; } }
            if (!match) return false;
        }
        return true;
    }

    static bool FitsMask(string fileName, string fileMask)
    {
        if (string.IsNullOrEmpty(fileMask) || fileMask == "*.*" || fileMask == "*") return true;
        if (fileMask.StartsWith("*") && !fileMask.Contains("?") && fileMask.IndexOf('*', 1) == -1)
            return fileName.EndsWith(fileMask.Substring(1), StringComparison.OrdinalIgnoreCase);
        try
        {
            string pattern = "^" + Regex.Escape(fileMask).Replace("\\*", ".*").Replace("\\?", ".") + "$";
            return Regex.IsMatch(fileName, pattern, RegexOptions.IgnoreCase);
        }
        catch { return true; }
    }

    static int Mask()
    {
        string? line;
        while ((line = Console.In.ReadLine()) != null)
        {
            var p = line.Split('\t');
            Console.WriteLine(IsFileAllowed(p[0], p[1], p[2]) ? "True" : "False");
        }
        return 0;
    }

    // ------------------------------------------------ AppSettings.Load (verbatim, WinForms types replaced)
    static int Ini(string[] args)
    {
        bool ShowTimeTab = false, ShowFilterPanel = false, ShowThroughputStats = false, ShowAdvancedBar = false,
             ShowFullPaths = false, ShowHashCol = true, ShowExpectedHashCol = true, LockColumns = true,
             EnableChecksumComments = false, ScanRecursive = true, HasCustomLocation = false;
        PathStorageMode PathStorageMode = PathStorageMode.Relative;
        ProcessingMode ProcessingMode = ProcessingMode.Auto;
        string PathPrefix = "", IncludePattern = "", ExcludePattern = "", CustomSignature = "L33T";
        HashType DefaultAlgo = HashType.XXHASH3;
        int SplitterDistance = -1, WW = 800, WH = 600, WX = 0, WY = 0;
        var ColumnOrder = new Dictionary<string, int>();

        var lines = File.ReadAllLines(args[1]);
        int w = 800, h = 600, x = 0, y = 0;
        bool foundX = false, foundY = false;
        foreach (var line in lines)
        {
            if (string.IsNullOrWhiteSpace(line) || line.StartsWith("[")) continue;
            var parts = line.Split(new[] { '=' }, 2);
            if (parts.Length != 2) continue;
            string key = parts[0].Trim();
            string val = parts[1].Trim();
            if (key.Equals("TimeTab", StringComparison.OrdinalIgnoreCase)) ShowTimeTab = (val == "1");
            else if (key.Equals("ShowThroughputStats", StringComparison.OrdinalIgnoreCase)) ShowThroughputStats = (val == "1");
            else if (key.Equals("ShowFilterPanel", StringComparison.OrdinalIgnoreCase)) ShowFilterPanel = (val == "1");
            else if (key.Equals("ShowAdvancedBar", StringComparison.OrdinalIgnoreCase)) ShowAdvancedBar = (val == "1");
            else if (key.Equals("ShowFullPaths", StringComparison.OrdinalIgnoreCase)) ShowFullPaths = (val == "1");
            else if (key.Equals("ShowHashCol", StringComparison.OrdinalIgnoreCase)) ShowHashCol = (val == "1");
            else if (key.Equals("ShowExpectedHashCol", StringComparison.OrdinalIgnoreCase)) ShowExpectedHashCol = (val == "1");
            else if (key.Equals("LockColumns", StringComparison.OrdinalIgnoreCase)) LockColumns = (val == "1");
            else if (key.Equals("PathStorageMode", StringComparison.OrdinalIgnoreCase)) { if (Enum.TryParse<PathStorageMode>(val, true, out var r)) PathStorageMode = r; }
            else if (key.Equals("UseAbsolutePaths", StringComparison.OrdinalIgnoreCase)) { if (val == "1") PathStorageMode = PathStorageMode.Absolute; }
            else if (key.Equals("ProcessingMode", StringComparison.OrdinalIgnoreCase)) { if (Enum.TryParse<ProcessingMode>(val, true, out var r)) ProcessingMode = r; }
            else if (key.Equals("OptimizeForHDD", StringComparison.OrdinalIgnoreCase)) { if (val == "1") ProcessingMode = ProcessingMode.HDD; }
            else if (key.Equals("EnableChecksumComments", StringComparison.OrdinalIgnoreCase)) EnableChecksumComments = (val == "1");
            else if (key.Equals("PathPrefix", StringComparison.OrdinalIgnoreCase)) PathPrefix = val;
            else if (key.Equals("IncludePattern", StringComparison.OrdinalIgnoreCase)) IncludePattern = val;
            else if (key.Equals("ExcludePattern", StringComparison.OrdinalIgnoreCase)) ExcludePattern = val;
            else if (key.Equals("ScanRecursive", StringComparison.OrdinalIgnoreCase)) ScanRecursive = (val == "1");
            else if (key.Equals("Signature", StringComparison.OrdinalIgnoreCase)) CustomSignature = val;
            else if (key.Equals("ColumnOrder", StringComparison.OrdinalIgnoreCase))
            {
                ColumnOrder.Clear();
                foreach (var pair in val.Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries))
                {
                    var kv = pair.Split(':');
                    if (kv.Length == 2 && int.TryParse(kv[1], out int idx)) ColumnOrder[kv[0]] = idx;
                }
            }
            else if (key.Equals("DefaultAlgo", StringComparison.OrdinalIgnoreCase)) { if (Enum.TryParse<HashType>(val, true, out var r)) DefaultAlgo = r; }
            else if (key.Equals("WindowSizeW", StringComparison.OrdinalIgnoreCase)) int.TryParse(val, out w);
            else if (key.Equals("WindowSizeH", StringComparison.OrdinalIgnoreCase)) int.TryParse(val, out h);
            else if (key.Equals("WindowPosX", StringComparison.OrdinalIgnoreCase)) { if (int.TryParse(val, out x)) foundX = true; }
            else if (key.Equals("WindowPosY", StringComparison.OrdinalIgnoreCase)) { if (int.TryParse(val, out y)) foundY = true; }
            else if (key.Equals("SplitterPos", StringComparison.OrdinalIgnoreCase)) { if (int.TryParse(val, out int tempDist)) SplitterDistance = tempDist; }
        }
        WW = w; WH = h;
        if (foundX && foundY) { WX = x; WY = y; HasCustomLocation = true; }

        string B(bool b) => b ? "1" : "0";
        Console.WriteLine($"tt={B(ShowTimeTab)} fp={B(ShowFilterPanel)} ts={B(ShowThroughputStats)} ab={B(ShowAdvancedBar)} full={B(ShowFullPaths)} hc={B(ShowHashCol)} ehc={B(ShowExpectedHashCol)} lock={B(LockColumns)}");
        Console.WriteLine($"psm={PathStorageMode} pm={ProcessingMode} cmt={B(EnableChecksumComments)} rec={B(ScanRecursive)} algo={DefaultAlgo}");
        Console.WriteLine($"prefix=[{PathPrefix}] inc=[{IncludePattern}] exc=[{ExcludePattern}] sig=[{CustomSignature}]");
        Console.WriteLine($"w={WW} h={WH} x={WX} y={WY} custom={B(HasCustomLocation)} split={SplitterDistance}");
        Console.WriteLine("cols=" + string.Join(",", ColumnOrder.Select(kv => $"{kv.Key}:{kv.Value}")));
        return 0;
    }

    // ------------------------------------------------ Creation: enumeration + ProcessFileEntry + PerformSaveAction
    // args: create <out> <algo> <base> <recursive 0/1> <advanced 0/1> <include> <exclude> <prefix> <comments 0/1> <absolute 0/1> <paths...>
    static int Create(string[] a)
    {
        string outPath = a[1];
        var algo = Enum.Parse<HashType>(a[2], true);
        string baseDir = a[3];
        bool recursive = a[4] == "1", advanced = a[5] == "1";
        string include = a[6], exclude = a[7], prefix = a[8];
        bool comments = a[9] == "1", absolute = a[10] == "1";
        var paths = a.Skip(11).ToArray();

        var names = new List<string>(); var rels = new List<string>(); var bases = new List<string>(); var hashes = new List<byte[]?>();
        var processed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        void Entry(string fullPath)
        {
            if (!processed.Add(fullPath)) return;
            if (advanced && !IsFileAllowed(Path.GetFileName(fullPath), include, exclude)) return;
            ReadOnlySpan<char> pathSpan = fullPath.AsSpan(); ReadOnlySpan<char> baseSpan = baseDir.AsSpan();
            string rel;
            if (pathSpan.StartsWith(baseSpan, StringComparison.OrdinalIgnoreCase) && pathSpan.Length > baseSpan.Length)
            {
                int offset = baseSpan.Length;
                if (pathSpan[offset] == Path.DirectorySeparatorChar || pathSpan[offset] == Path.AltDirectorySeparatorChar) offset++;
                rel = pathSpan.Slice(offset).ToString();
            }
            else
            {
                try { rel = Path.GetRelativePath(baseDir, fullPath); } catch { rel = Path.GetFileName(fullPath); }
            }
            names.Add(Path.GetFileName(fullPath)); rels.Add(rel); bases.Add(baseDir); hashes.Add(HashFile(fullPath, algo));
        }

        foreach (string path in paths)
        {
            if (File.Exists(path)) Entry(path);
            else if (Directory.Exists(path))
            {
                var opts = new EnumerationOptions { IgnoreInaccessible = true, RecurseSubdirectories = recursive, AttributesToSkip = FileAttributes.ReparsePoint | FileAttributes.Hidden | FileAttributes.System };
                foreach (string file in Directory.EnumerateFiles(path, "*", opts)) Entry(file);
            }
        }

        string saveDirectory = Path.GetDirectoryName(outPath) ?? "";
        using (StreamWriter sw = new StreamWriter(outPath))
        {
            if (comments)
            {
                sw.WriteLine($"; Generated by SharpSFV (Signature: L33T)");
                sw.WriteLine($"; Algorithm: {algo}");
            }
            for (int i = 0; i < names.Count; i++)
            {
                if (hashes[i] == null) continue;
                string hash = Convert.ToHexString(hashes[i]!);
                string fullPath = Path.Combine(bases[i], rels[i]);
                string pathToWrite = fullPath;
                if (!absolute)
                {
                    try
                    {
                        string relPath = Path.GetRelativePath(saveDirectory, fullPath);
                        if (Path.IsPathRooted(relPath) && !relPath.StartsWith(".")) pathToWrite = rels[i] ?? Path.GetFileName(fullPath);
                        else pathToWrite = relPath;
                    }
                    catch { pathToWrite = fullPath; }
                }
                if (advanced && !string.IsNullOrEmpty(prefix)) pathToWrite = Path.Combine(prefix, pathToWrite);
                sw.WriteLine($"{hash} *{pathToWrite}");
            }
        }
        return 0;
    }

    // ------------------------------------------------ Verification statuses (sequential, same rules incl. endian fallback)
    static int Verify(string[] a)
    {
        string checkFilePath = a[1];
        var fallback = Enum.Parse<HashType>(a[2], true);
        var algo = DetectAlgo(checkFilePath, fallback);
        var (lines, _) = ParseFile(checkFilePath, algo);
        string baseFolder = Path.GetDirectoryName(checkFilePath) ?? "";
        int ok = 0, bad = 0, missing = 0; bool legacy = false;
        foreach (var (expected, filename) in lines)
        {
            string fullPath = Path.Combine(baseFolder, filename);
            string status;
            if (!File.Exists(fullPath)) { status = "Missing"; missing++; }
            else
            {
                var calc = HashFile(fullPath, algo);
                if (calc == null) { status = "Error"; bad++; }
                else if (calc.SequenceEqual(expected)) { status = "OK"; ok++; }
                else
                {
                    bool recovered = false;
                    if (algo == HashType.Crc32) { var r = (byte[])calc.Clone(); Array.Reverse(r); if (r.SequenceEqual(expected)) { recovered = true; legacy = true; } }
                    if (recovered) { status = "OK"; ok++; } else { status = "Bad"; bad++; }
                }
            }
            Console.WriteLine($"{status}|{filename}");
        }
        Console.WriteLine($"OK={ok} BAD={bad} MISSING={missing} LEGACY={legacy}");
        return 0;
    }
}
