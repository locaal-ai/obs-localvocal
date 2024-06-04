# Define the output CSV file
$outputCsv = "output.csv"

# Create or clear the CSV file and add the headers
"Iteration,Number1,Number2,OutputFile" | Out-File -FilePath $outputCsv

# Loop 10 times
for ($i = 1; $i -le 10; $i++) {
    # Run the first command and capture the output to a file
    & "C:\Users\roysh\Downloads\obs-localvocal\release\Release\test\obs-localvocal-tests.exe" "C:\Users\roysh\Downloads\obs-localvocal\src\tests\KO_gyu_0227(굥이)20_Mar_3356_1_50.mp3" "C:\Users\roysh\Downloads\obs-localvocal\src\tests\config.json"
    # read the contents of the output.txt file
    $command1OutputFile = Get-Content -Path "output.txt"

    # Run the second command and capture the output
    $command2Output = python .\evaluate_output.py "C:\Users\roysh\Downloads\obs-localvocal\src\tests\KO_gyu_0227(굥이)20_Mar_3356_1_50.txt" .\output.txt

    # Append the results to the CSV file
    "$i,$command2Output,$command1OutputFile" | Out-File -FilePath $outputCsv -Append
}
