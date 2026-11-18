$ErrorActionPreference='Stop'
$schema=New-Object System.Xml.Schema.XmlSchemaSet
$schema.Add($null,(Resolve-Path 'schemas/northstar-ui-v3.xsd').Path) | Out-Null
$schema.Compile()
$errors=New-Object System.Collections.Generic.List[string]
$settings=New-Object System.Xml.XmlReaderSettings
$settings.Schemas=$schema
$settings.ValidationType=[System.Xml.ValidationType]::Schema
$settings.DtdProcessing=[System.Xml.DtdProcessing]::Prohibit
$settings.add_ValidationEventHandler({
  param($sender,$eventArgs)
  $errors.Add($eventArgs.Message)
})
$folders=@('sharedAssets/ui','engine/northstarcpp/resources/sharedAssets/ui','Projects','tools/templates')
$files=@($folders | ForEach-Object {Get-ChildItem $_ -Recurse -File -Filter '*.nui'})
foreach($file in $files){
  $r=[System.Xml.XmlReader]::Create($file.FullName,$settings)
  try{while($r.Read()){}}
  finally{$r.Dispose()}
}
if($errors.Count -gt 0){$errors | Select-Object -First 25;exit 1}
Write-Output "NUI_XML_SCHEMA_PASS $($files.Count) XML files"
