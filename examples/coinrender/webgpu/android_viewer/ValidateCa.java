import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;

/** Host-only build validator: exports exactly one public X.509 CA as DER. */
public final class ValidateCa {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) throw new IllegalArgumentException("Expected public CA input and DER output");
        Path source = Paths.get(args[0]);
        long size = Files.size(source);
        if (size < 1 || size > 131072) throw new IllegalArgumentException("Invalid CA file size");
        byte[] bytes = Files.readAllBytes(source);
        if (new String(bytes, StandardCharsets.ISO_8859_1).contains("PRIVATE KEY")) {
            throw new IllegalArgumentException("CA input contains a private key; public certificate only");
        }
        ByteArrayInputStream input = new ByteArrayInputStream(bytes);
        X509Certificate certificate = (X509Certificate) CertificateFactory.getInstance("X.509").generateCertificate(input);
        int extra;
        while ((extra = input.read()) != -1) {
            if (!Character.isWhitespace((char) extra)) throw new IllegalArgumentException("Extra data after certificate");
        }
        certificate.checkValidity();
        if (certificate.getBasicConstraints() < 0) throw new IllegalArgumentException("Certificate is not a CA");
        boolean[] usage = certificate.getKeyUsage();
        if (usage != null && (usage.length <= 5 || !usage[5])) throw new IllegalArgumentException("CA does not permit certificate signing");
        Files.write(Paths.get(args[1]), certificate.getEncoded());
        System.out.println("Public CA validated: " + certificate.getSubjectX500Principal().getName());
    }
}
