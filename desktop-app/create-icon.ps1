Add-Type -AssemblyName System.Drawing

$bitmap = New-Object System.Drawing.Bitmap(1024, 1024)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)

# Fill with blue background
$blue = [System.Drawing.Color]::FromArgb(79, 70, 229)
$graphics.Clear($blue)

# Draw "S" for SnapLLM
$font = New-Object System.Drawing.Font("Arial", 600, [System.Drawing.FontStyle]::Bold)
$brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
$graphics.DrawString("S", $font, $brush, 150, 100)

# Save as PNG
$bitmap.Save("app-icon.png", [System.Drawing.Imaging.ImageFormat]::Png)

# Cleanup
$graphics.Dispose()
$font.Dispose()
$brush.Dispose()
$bitmap.Dispose()

Write-Host "Icon created successfully!"
