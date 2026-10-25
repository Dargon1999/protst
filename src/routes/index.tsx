import { createFileRoute } from "@tanstack/react-router";
import { RamApp } from "@/components/ram-app";

export const Route = createFileRoute("/")({ component: Home });

function Home() {
  return <RamApp />;
}
