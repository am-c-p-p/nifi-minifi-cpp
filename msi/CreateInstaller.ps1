function Error {
 Param ([string]$text)
 
 Write-Error "$text" -ErrorAction Stop
}

function Info {
 Param ([string]$text)
 
 Write-Host "$text" -ForegroundColor Yellow 
}

Function CheckPath {
 Param ([string]$info, [string]$path)

 If (-Not (Test-Path $path)) {
  Error("Failed to find $path.")
 }

 Info("$info -> $path")
}

Function CreateNewDir {
  Param ([string]$path)

  If (Test-Path $path) {
    Remove-Item "$path" -Force -Recurse
  }
  If (Test-Path $path) {
    Error("Cannot delete $path.");
  }
 
  md $path > $null
  If (-Not (Test-Path $path)) {
    Error("Cannot create $path.");
  }
}

Function DeleteDir {
  Param ([string]$path)
  
  If (Test-Path "$path") {
    Remove-Item "$path" -Force -Recurse
    If (Test-Path "$path") {
      Error("Cannot delete $path");
    }
  }
}


CheckPath "dark.exe" "$Env:WIX\bin\dark.exe"

# Get vswhere.exe path.
If ($vsWhere = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue) {
  $vsWhere = $vsWhere.Path
} ElseIf (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe") {
  $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
} Else {
  Error("vswhere not found.")
}
CheckPath "vsWhere" "$vsWhere"

# Get path to Visual Studio installation using vswhere.
$vsPath = &$vsWhere -latest -products * -version "[15.0,16.0)" -requires Microsoft.Component.MSBuild -property installationPath
If ([string]::IsNullOrEmpty("$vsPath")) {
  Error("Failed to find Visual Studio 2017 installation") 
}
CheckPath "Visual Studio installation" "${vsPath}"

If (Test-Path Env:VCToolsRedistDir) {
  Info("Visual Studio Command Prompt variables already set.")
} Else {
  # Load VC vars
  Push-Location "${vsPath}\VC\Auxiliary\Build"
  cmd /c "vcvarsall.bat x64&set" | ForEach-Object {
      If ($_ -match "=") {
        $v = $_.split("="); Set-Item -Force -Path "ENV:\$($v[0])" -Value "$($v[1])"
      }
    }
  Pop-Location
  Info("Visual Studio Command Prompt variables set.")
}
CheckPath "VCToolsRedistDir" "$Env:VCToolsRedistDir"

$VSMergeModules = "$Env:VCToolsRedistDir\MergeModules"
CheckPath "VSMergeModules" "$VSMergeModules"

$MergeModules = "$PSScriptRoot\mergeModules"
CreateNewDir("$mergeModules")

$Redist = "$PSScriptRoot\redist"
CreateNewDir("$redist")

# Set $Build64
If (Test-Path "$PSScriptRoot\x64") {
	Info("Using x64 build")
	$Build64 = "x64"
} Else {
  If (Test-Path "$PSScriptRoot\Win32") {
	Info("Using x86 build")
	$Build64 = "x86"
  } Else {
	Error("x64 and Win32 directories are not found.")
  }
}

$MergeModulesFile = "$MergeModules\File"
DeleteDir("$MergeModulesFile")

&"$Env:WIX\bin\dark.exe" "$VSMergeModules\Microsoft_VC141_CRT_$Build64.msm" -x "$MergeModules" > $null
CheckPath "MergeModulesFile" "$MergeModulesFile"

# Create $WSIRedistDlls
Get-ChildItem "$MergeModulesFile" | Foreach-Object {
  $fileName = "$_"
  
  $posDll = $fileName.LastIndexOf(".dll.")
  if (-1 -eq $posDll) {
	Error("No '.dll.' in $fileName")
  }
  
  $fileNameNoDll = $fileName.substring(0, $posDll)
  
  $fileNameDll = $fileNameNoDll + ".dll"
  
  Copy-Item -Path "$MergeModulesFile\$fileName" -Destination "$Redist\$fileNameDll" -ErrorAction Stop > $null
  
  $WSIRedistDlls = $WSIRedistDlls + "`t`t`t`t<File Id=`"%ServiceType%_$fileNameNoDll`" Name=`"$fileNameDll`" KeyPath=`"no`" Source=`"redist\$fileNameDll`"/>`n"
}
# Remove last \n"
$WSIRedistDlls = $WSIRedistDlls.Substring(0, $WSIRedistDlls.Length-1)

$MinifiServiceRedistDlls = $WSIRedistDlls -replace "%ServiceType%", "minifiService"
$MinifiServiceNotLocalRedistDlls = $WSIRedistDlls -replace "%ServiceType%", "minifiServiceNotLocal"

$WSI = Get-Content -path "$PSScriptRoot\..\msi\WixWin.wsi" -Raw

$WSI = $WSI.Replace("%minifiServiceRedistDlls%", "$MinifiServiceRedistDlls")
$WSI = $WSI.Replace("%minifiServiceNotLocalRedistDlls%", "$MinifiServiceNotLocalRedistDlls")

$WSI | Set-Content -Path "$PSScriptRoot\WixWin.wsi"

&cpack
