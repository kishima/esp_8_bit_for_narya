require 'rake'
require 'fileutils'

SUBPROJECTS = ["core", "hid"]

# Run a rake task in each subproject directory.
def run_in_subprojects(task_name, description = nil, only: nil)
  results = []
  targets = only ? [only] : SUBPROJECTS

  targets.each do |name|
    unless Dir.exist?(name)
      puts "[!] #{name}/ directory not found, skipping..."
      results << false
      next
    end
    unless File.exist?(File.join(name, 'Rakefile'))
      puts "[!] #{name}/Rakefile not found, skipping..."
      results << nil
      next
    end

    action = description || "Running 'rake #{task_name}' in"
    puts "=" * 60
    puts "#{action} #{name}..."
    puts "=" * 60

    Dir.chdir(name) do
      begin
        sh "rake #{task_name}"
        puts "[ok] #{name} #{task_name}"
        results << true
      rescue => e
        puts "[fail] #{name}: #{e.message}"
        results << false
      end
    end
  end

  ok = results.count(true)
  ng = results.count(false)
  skip = results.count(nil)
  puts "=" * 60
  puts "Summary: #{ok} ok, #{ng} fail, #{skip} skip"
  puts "=" * 60
  ng == 0
end

desc "Build core then hid"
task :build do
  run_in_subprojects("build", "Building")
end

desc "Build only core"
task :"build:core" do
  run_in_subprojects("build", "Building", only: "core")
end

desc "Build only hid"
task :"build:hid" do
  run_in_subprojects("build", "Building", only: "hid")
end

desc "Clean build artifacts in each subproject"
task :clean do
  run_in_subprojects("clean", "Cleaning")
end

desc "Full clean (drop sdkconfig too) in each subproject"
task :clean_all do
  run_in_subprojects("clean_all", "Cleaning all")
end

desc "List tasks"
task :help do
  sh "rake -T"
end

task :default => :help
